/*
 * aswap - optimized character swap tool for hashcat dictionary mutation
 *
 * Usage: cat dict.txt | ./aswap 2o0 1e3
 *        ./aswap 2o0 1e3 < dict.txt
 *
 * Rule format: <levels><find><replace>
 *   levels: 1-9, how many occurrences deep to consider
 *   find:   character to find
 *   replace: character to replace with
 *
 * Optimizations:
 *   - Zero-allocation backtracking (no string copies)
 *   - mmap for file input, zero-copy line parsing
 *   - Buffered fwrite output (no iostream)
 *   - FNV-1a hash dedup (8 bytes vs 40+ for string sets)
 *   - Thread-local buffers, single flush per thread
 *   - memchr pre-filter for non-matching lines
 *   - Global cross-thread dedup via hash merge
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ── Types ───────────────────────────────────────────────────────────────────

struct Rule {
    int levels;
    char find;
    char replace;
};

struct Line {
    const char* ptr;
    size_t len;
};

struct Entry {
    size_t offset;
    size_t len;
    uint64_t hash;
};

struct OutputBuffer {
    char* data;
    size_t size;
    size_t cap;

    OutputBuffer() : size(0), cap(1 << 20) {
        data = static_cast<char*>(malloc(cap));
    }
    ~OutputBuffer() { free(data); }

    void append(const char* buf, size_t len) {
        size_t need = len + 1;
        if (__builtin_expect(size + need > cap, 0)) {
            while (size + need > cap) cap <<= 1;
            data = static_cast<char*>(realloc(data, cap));
        }
        memcpy(data + size, buf, len);
        data[size + len] = '\n';
        size += need;
    }
};

// ── Globals ─────────────────────────────────────────────────────────────────

static std::vector<Rule> g_rules;
static std::mutex g_out_mutex;
static std::unordered_set<uint64_t> g_seen;

// ── FNV-1a 64-bit ──────────────────────────────────────────────────────────

static inline uint64_t fnv1a(const char* buf, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(buf[i]));
        h *= 0x100000001b3ULL;
    }
    return h;
}

// ── Emit helper ─────────────────────────────────────────────────────────────

static inline void emit(const char* buf, size_t len,
                        std::unordered_set<uint64_t>& seen,
                        OutputBuffer& out, std::vector<Entry>& entries) {
    uint64_t h = fnv1a(buf, len);
    if (seen.insert(h).second) {
        entries.push_back({out.size, len, h});
        out.append(buf, len);
    }
}

// ── Single-rule generator (fast path) ───────────────────────────────────────
// "levels" = how many occurrences of find to consider.
// Each considered occurrence branches: keep original or swap.

static void gen1(char* buf, size_t len, const Rule& r,
                 size_t pos, int depth,
                 std::unordered_set<uint64_t>& seen,
                 OutputBuffer& out, std::vector<Entry>& entries) {
    // Find next occurrence
    size_t i = pos;
    while (i < len && buf[i] != r.find) ++i;

    if (i >= len || depth >= r.levels) {
        emit(buf, len, seen, out, entries);
        return;
    }

    // Keep original
    gen1(buf, len, r, i + 1, depth + 1, seen, out, entries);
    // Swap
    buf[i] = r.replace;
    gen1(buf, len, r, i + 1, depth + 1, seen, out, entries);
    buf[i] = r.find;
}

// ── Multi-rule generator ────────────────────────────────────────────────────

static void gen(char* buf, size_t len,
                size_t ri, size_t pos, int depth,
                std::unordered_set<uint64_t>& seen,
                OutputBuffer& out, std::vector<Entry>& entries) {
    const Rule& r = g_rules[ri];

    size_t i = pos;
    while (i < len && buf[i] != r.find) ++i;

    if (i >= len || depth >= r.levels) {
        if (ri + 1 >= g_rules.size())
            emit(buf, len, seen, out, entries);
        else
            gen(buf, len, ri + 1, 0, 0, seen, out, entries);
        return;
    }

    // Keep
    gen(buf, len, ri, i + 1, depth + 1, seen, out, entries);
    // Swap
    buf[i] = r.replace;
    gen(buf, len, ri, i + 1, depth + 1, seen, out, entries);
    buf[i] = r.find;
}

// ── Pre-filter ──────────────────────────────────────────────────────────────

static inline bool has_target(const char* p, size_t len) {
    for (const auto& r : g_rules)
        if (memchr(p, r.find, len)) return true;
    return false;
}

// ── Worker ──────────────────────────────────────────────────────────────────

static void worker(const std::vector<Line>& lines, std::atomic<size_t>& idx) {
    std::unordered_set<uint64_t> seen;
    seen.reserve(4096);
    OutputBuffer out;
    std::vector<Entry> entries;
    entries.reserve(8192);
    char buf[4096];

    size_t i;
    while ((i = idx.fetch_add(1, std::memory_order_relaxed)) < lines.size()) {
        const auto& ln = lines[i];
        if (ln.len == 0 || ln.len >= sizeof(buf)) continue;
        memcpy(buf, ln.ptr, ln.len);

        if (!has_target(buf, ln.len)) {
            emit(buf, ln.len, seen, out, entries);
            continue;
        }

        if (g_rules.size() == 1)
            gen1(buf, ln.len, g_rules[0], 0, 0, seen, out, entries);
        else
            gen(buf, ln.len, 0, 0, 0, seen, out, entries);
    }

    // Flush with global dedup
    std::lock_guard<std::mutex> lock(g_out_mutex);
    for (const auto& e : entries) {
        if (g_seen.insert(e.hash).second)
            fwrite(out.data + e.offset, 1, e.len + 1, stdout);
    }
}

// ── Line parsing ────────────────────────────────────────────────────────────

static std::vector<Line> parse_lines(const char* data, size_t sz) {
    std::vector<Line> lines;
    lines.reserve(sz / 8);
    const char* p = data;
    const char* end = data + sz;

    while (p < end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
        size_t len = nl ? static_cast<size_t>(nl - p) : static_cast<size_t>(end - p);

        size_t tr = len;
        while (tr > 0 && (p[tr - 1] == '\r' || p[tr - 1] == '\0')) --tr;

        bool blank = true;
        for (size_t i = 0; i < tr; ++i) {
            if (p[i] != ' ' && p[i] != '\t') { blank = false; break; }
        }

        if (!blank && tr > 0) lines.push_back({p, tr});
        p += len + (nl ? 1 : 0);
    }
    return lines;
}

// ── Read stdin for pipes ────────────────────────────────────────────────────

static std::pair<char*, size_t> read_stdin_buf() {
    size_t cap = 1 << 20, sz = 0;
    char* buf = static_cast<char*>(malloc(cap));
    while (true) {
        if (sz + 65536 > cap) { cap <<= 1; buf = static_cast<char*>(realloc(buf, cap)); }
        ssize_t n = read(STDIN_FILENO, buf + sz, cap - sz);
        if (n <= 0) break;
        sz += n;
    }
    return {buf, sz};
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: ./aswap 2o0 1e3\n"); return 1; }

    g_rules.reserve(argc - 1);
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (strlen(a) != 3 || !isdigit(static_cast<unsigned char>(a[0]))) {
            fprintf(stderr, "Invalid rule: %s\n", a); return 1;
        }
        g_rules.push_back({a[0] - '0', a[1], a[2]});
    }

    const char* input = nullptr;
    size_t input_sz = 0;
    bool mmapped = false;
    char* heap_buf = nullptr;

    struct stat st;
    if (fstat(STDIN_FILENO, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        void* m = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, STDIN_FILENO, 0);
        if (m != MAP_FAILED) {
            madvise(m, st.st_size, MADV_SEQUENTIAL);
            input = static_cast<const char*>(m);
            input_sz = st.st_size;
            mmapped = true;
        }
    }
    if (!mmapped) {
        auto [b, s] = read_stdin_buf();
        heap_buf = b; input = b; input_sz = s;
    }

    if (input_sz == 0) goto done;
    {
        auto lines = parse_lines(input, input_sz);
        if (lines.empty()) goto done;

        size_t nt = std::max<size_t>(1, std::thread::hardware_concurrency());
        std::atomic<size_t> idx{0};

        if (nt == 1) {
            worker(lines, idx);
        } else {
            std::vector<std::thread> threads;
            threads.reserve(nt);
            for (size_t t = 0; t < nt; ++t)
                threads.emplace_back(worker, std::cref(lines), std::ref(idx));
            for (auto& t : threads) t.join();
        }
    }

done:
    if (mmapped) munmap(const_cast<char*>(input), input_sz);
    else free(heap_buf);
    return 0;
}
