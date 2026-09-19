/*
 * aswap - advanced character swapper for rule based password cracking
 *
 * Usage: cat dict.txt | ./aswap 2o0 1e3
 *        ./aswap --unique 2o0 1e3 < dict.txt
 *
 * Rule format: <levels><find><replace>
 *   levels:  1-9, how many occurrences of <find> to branch on (0 is an error)
 *   find:    character to find
 *   replace: character to replace with
 *
 * Two modes:
 *   streaming (default) - dedups per input word and writes candidates as they
 *                     are produced, so a consumer (hashcat) can start work
 *                     immediately. Constant memory.
 *   --unique        - additionally dedups across the whole run, so a candidate
 *                     two different input words both produce is emitted once.
 *                     Requires holding every candidate in memory and writing
 *                     nothing until all input has been read.
 *
 * Optimizations:
 *   - Zero-allocation backtracking (no string copies)
 *   - mmap for file input, zero-copy line parsing (--unique mode)
 *   - Buffered output, one write per thread flush
 *   - FNV-1a hash dedup (8 bytes vs 40+ for string sets)
 *   - memchr pre-filter for non-matching lines
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ── Tunables ────────────────────────────────────────────────────────────────

static const size_t OUT_FLUSH_BYTES = 1 << 18;  // 256 KiB before a stream flush
static const size_t READ_CHUNK      = 1 << 18;  // stdin read size (stream mode)
static const size_t BATCH_LINES     = 1024;     // max input words per queue item
static const size_t QUEUE_DEPTH     = 64;       // bounded queue, caps memory

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

    OutputBuffer(const OutputBuffer&) = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;

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

// ── Output ──────────────────────────────────────────────────────────────────

// Hand a filled buffer to stdout and empty it. Caller must not hold the lock.
static void flush_buffer(OutputBuffer& out, bool force_flush) {
    if (out.size == 0) return;
    std::lock_guard<std::mutex> lock(g_out_mutex);
    fwrite(out.data, 1, out.size, stdout);
    if (force_flush) fflush(stdout);
    out.size = 0;
}

// ── Sinks ───────────────────────────────────────────────────────────────────
// Unique: dedup per thread, keep every candidate until the thread is done, then
// merge against the global set so the run as a whole emits each candidate once.

struct UniqueSink {
    std::unordered_set<uint64_t>& seen;
    OutputBuffer& out;
    std::vector<Entry>& entries;

    inline void emit(const char* buf, size_t len) {
        uint64_t h = fnv1a(buf, len);
        if (seen.insert(h).second) {
            entries.push_back({out.size, len, h});
            out.append(buf, len);
        }
    }
};

// Stream: dedup only within the current input word, and push bytes out as soon
// as enough have piled up. Memory stays flat no matter how long the run is.
// This is the default: on a normal (already deduplicated) wordlist the global
// pass below finds nothing to remove, so it is pure cost.

struct StreamSink {
    std::unordered_set<uint64_t>& seen;
    OutputBuffer& out;

    inline void emit(const char* buf, size_t len) {
        if (seen.insert(fnv1a(buf, len)).second) {
            out.append(buf, len);
            if (__builtin_expect(out.size >= OUT_FLUSH_BYTES, 0))
                flush_buffer(out, true);
        }
    }
};

// ── Single-rule generator (fast path) ───────────────────────────────────────
// "levels" = how many occurrences of find to consider.
// Each considered occurrence branches: keep original or swap.

template <typename Sink>
static void gen1(char* buf, size_t len, const Rule& r,
                 size_t pos, int depth, Sink& sink) {
    // Find next occurrence
    size_t i = pos;
    while (i < len && buf[i] != r.find) ++i;

    if (i >= len || depth >= r.levels) {
        sink.emit(buf, len);
        return;
    }

    // Keep original
    gen1(buf, len, r, i + 1, depth + 1, sink);
    // Swap
    buf[i] = r.replace;
    gen1(buf, len, r, i + 1, depth + 1, sink);
    buf[i] = r.find;
}

// ── Multi-rule generator ────────────────────────────────────────────────────

template <typename Sink>
static void gen(char* buf, size_t len,
                size_t ri, size_t pos, int depth, Sink& sink) {
    const Rule& r = g_rules[ri];

    size_t i = pos;
    while (i < len && buf[i] != r.find) ++i;

    if (i >= len || depth >= r.levels) {
        if (ri + 1 >= g_rules.size())
            sink.emit(buf, len);
        else
            gen(buf, len, ri + 1, 0, 0, sink);
        return;
    }

    // Keep
    gen(buf, len, ri, i + 1, depth + 1, sink);
    // Swap
    buf[i] = r.replace;
    gen(buf, len, ri, i + 1, depth + 1, sink);
    buf[i] = r.find;
}

// ── Pre-filter ──────────────────────────────────────────────────────────────

static inline bool has_target(const char* p, size_t len) {
    for (const auto& r : g_rules)
        if (memchr(p, r.find, len)) return true;
    return false;
}

// ── Expand one word ─────────────────────────────────────────────────────────
// scratch grows to fit the word, so there is no line length limit.

template <typename Sink>
static void expand(const char* word, size_t len,
                   std::vector<char>& scratch, Sink& sink) {
    if (len == 0) return;
    if (scratch.size() < len) scratch.resize(len);
    char* buf = scratch.data();
    memcpy(buf, word, len);

    if (!has_target(buf, len)) {
        sink.emit(buf, len);
        return;
    }

    if (g_rules.size() == 1)
        gen1(buf, len, g_rules[0], 0, 0, sink);
    else
        gen(buf, len, 0, 0, 0, sink);
}

// ── Line trimming ───────────────────────────────────────────────────────────
// Drop trailing CR/NUL, skip lines that are empty or all blanks.

static inline bool trim_line(const char* p, size_t len, size_t& kept) {
    size_t tr = len;
    while (tr > 0 && (p[tr - 1] == '\r' || p[tr - 1] == '\0')) --tr;
    for (size_t i = 0; i < tr; ++i) {
        if (p[i] != ' ' && p[i] != '\t') { kept = tr; return true; }
    }
    return false;  // empty or all blanks
}

// ════════════════════════════════════════════════════════════════════════════
// --unique mode: read everything, dedup across the whole run, write at the end
// ════════════════════════════════════════════════════════════════════════════

static std::vector<Line> parse_lines(const char* data, size_t sz) {
    std::vector<Line> lines;
    lines.reserve(sz / 8);
    const char* p = data;
    const char* end = data + sz;

    while (p < end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
        size_t len = nl ? static_cast<size_t>(nl - p) : static_cast<size_t>(end - p);

        size_t kept;
        if (trim_line(p, len, kept)) lines.push_back({p, kept});
        p += len + (nl ? 1 : 0);
    }
    return lines;
}

static void unique_worker(const std::vector<Line>& lines, std::atomic<size_t>& idx) {
    std::unordered_set<uint64_t> seen;
    seen.reserve(4096);
    OutputBuffer out;
    std::vector<Entry> entries;
    entries.reserve(8192);
    std::vector<char> scratch(256);
    UniqueSink sink{seen, out, entries};

    size_t i;
    while ((i = idx.fetch_add(1, std::memory_order_relaxed)) < lines.size())
        expand(lines[i].ptr, lines[i].len, scratch, sink);

    // Flush with global dedup
    std::lock_guard<std::mutex> lock(g_out_mutex);
    for (const auto& e : entries) {
        if (g_seen.insert(e.hash).second)
            fwrite(out.data + e.offset, 1, e.len + 1, stdout);
    }
}

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

static int run_unique(size_t nthreads) {
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

    if (input_sz > 0) {
        auto lines = parse_lines(input, input_sz);
        if (!lines.empty()) {
            std::atomic<size_t> idx{0};
            if (nthreads == 1) {
                unique_worker(lines, idx);
            } else {
                std::vector<std::thread> threads;
                threads.reserve(nthreads);
                for (size_t t = 0; t < nthreads; ++t)
                    threads.emplace_back(unique_worker, std::cref(lines), std::ref(idx));
                for (auto& t : threads) t.join();
            }
        }
    }

    if (mmapped) munmap(const_cast<char*>(input), input_sz);
    else free(heap_buf);
    return 0;
}

// ════════════════════════════════════════════════════════════════════════════
// Default mode: stream candidates out as they are produced
// ════════════════════════════════════════════════════════════════════════════

using WordBatch = std::vector<std::string>;

class BatchQueue {
    std::mutex m_;
    std::condition_variable not_full_, not_empty_;
    std::deque<WordBatch> q_;
    bool done_ = false;

public:
    void push(WordBatch&& b) {
        std::unique_lock<std::mutex> lk(m_);
        not_full_.wait(lk, [&] { return q_.size() < QUEUE_DEPTH; });
        q_.push_back(std::move(b));
        not_empty_.notify_one();
    }

    bool pop(WordBatch& out) {
        std::unique_lock<std::mutex> lk(m_);
        not_empty_.wait(lk, [&] { return !q_.empty() || done_; });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void finish() {
        std::lock_guard<std::mutex> lk(m_);
        done_ = true;
        not_empty_.notify_all();
    }
};

// Reads stdin incrementally. A batch is handed off as soon as the read buffer
// is drained, so a slow producer upstream still gets its words processed right
// away instead of waiting for EOF.
static void reader(BatchQueue& q) {
    std::vector<char> buf(READ_CHUNK);
    std::string carry;
    WordBatch batch;
    batch.reserve(BATCH_LINES);

    auto add = [&](const char* p, size_t len) {
        size_t kept;
        if (trim_line(p, len, kept)) batch.emplace_back(p, kept);
    };
    auto ship = [&] {
        if (batch.empty()) return;
        q.push(std::move(batch));
        batch.clear();
        batch.reserve(BATCH_LINES);
    };

    while (true) {
        ssize_t n = read(STDIN_FILENO, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;

        const char* p = buf.data();
        const char* end = p + n;
        while (p < end) {
            const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
            if (!nl) { carry.append(p, end - p); break; }

            if (carry.empty()) {
                add(p, static_cast<size_t>(nl - p));
            } else {
                carry.append(p, static_cast<size_t>(nl - p));
                add(carry.data(), carry.size());
                carry.clear();
            }
            if (batch.size() >= BATCH_LINES) ship();
            p = nl + 1;
        }
        ship();  // input drained: push what we have rather than block on it
    }

    if (!carry.empty()) add(carry.data(), carry.size());
    ship();
    q.finish();
}

static void stream_worker(BatchQueue& q) {
    std::unordered_set<uint64_t> seen;
    seen.reserve(4096);
    OutputBuffer out;
    std::vector<char> scratch(256);
    StreamSink sink{seen, out};
    WordBatch batch;

    while (q.pop(batch)) {
        for (const auto& word : batch) {
            seen.clear();  // dedup scope is a single input word
            expand(word.data(), word.size(), scratch, sink);
        }
        flush_buffer(out, true);  // batch done: let the consumer see it
    }
    flush_buffer(out, true);
}

static int run_stream(size_t nthreads) {
    BatchQueue q;
    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (size_t t = 0; t < nthreads; ++t)
        workers.emplace_back(stream_worker, std::ref(q));

    reader(q);
    for (auto& t : workers) t.join();
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

static void usage(FILE* f) {
    fprintf(f,
        "Usage: cat dict.txt | aswap [--unique] <rule> [rule ...]\n"
        "\n"
        "Rule format: <levels><find><replace>   e.g. 2o0 1e3\n"
        "  levels  1-9, how many occurrences of <find> to branch on\n"
        "\n"
        "By default candidates are written as they are produced, so a consumer\n"
        "such as hashcat can start before aswap has read all of its input, and\n"
        "memory stays flat however long the run is.\n"
        "\n"
        "Options:\n"
        "  --unique   also remove candidates that two different input words both\n"
        "             produce. Only finds anything if the wordlist already holds\n"
        "             leet variants (love and l0ve). Costs: every candidate is\n"
        "             held in memory and nothing is written until all input has\n"
        "             been read.\n"
        "  -h, --help show this help\n");
}

int main(int argc, char* argv[]) {
    bool unique = false;

    std::vector<const char*> rule_args;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "--unique") || !strcmp(a, "-u")) { unique = true; continue; }
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
        rule_args.push_back(a);
    }

    if (rule_args.empty()) { usage(stderr); return 1; }

    g_rules.reserve(rule_args.size());
    for (const char* a : rule_args) {
        if (strlen(a) != 3 || !isdigit(static_cast<unsigned char>(a[0]))) {
            fprintf(stderr, "Invalid rule: %s (expected <levels><find><replace>, e.g. 2o0)\n", a);
            return 1;
        }
        if (a[0] == '0') {
            fprintf(stderr, "Invalid rule: %s (levels must be 1-9)\n", a);
            return 1;
        }
        g_rules.push_back({a[0] - '0', a[1], a[2]});
    }

    setvbuf(stdout, nullptr, _IOFBF, 1 << 20);

    size_t nthreads = std::max<size_t>(1, std::thread::hardware_concurrency());
    int rc = unique ? run_unique(nthreads) : run_stream(nthreads);

    fflush(stdout);
    return rc;
}
