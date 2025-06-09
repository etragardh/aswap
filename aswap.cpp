#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <cctype>

// Rule definition
struct Rule {
  int levels;
  char find;
  char replace;
};

// Check if string is all whitespace (space, tab, etc.)
bool is_blank(const std::string& s) {
  return std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

// Clean string by keeping only printable ASCII characters (space to ~)
std::string clean_input(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  for (unsigned char c : s) {
    if (c >= 32 && c <= 126) {
      result += c;
    } else if (c == '\t') {
      result += ' ';
    }
    // skip nulls, control chars, extended bytes (e.g. UTF8 fragments)
  }
  return result;
}

// Apply a single rule to a word
std::vector<std::string> apply_rule(const std::string& word, const Rule& rule) {
  std::vector<std::string> results;
  results.reserve(16);
  results.push_back(word);

  int depth = 0;

  for (size_t i = 0; i < word.size(); ++i) {
    if (word[i] != rule.find) continue;

    size_t count = results.size();
    for (size_t j = 0; j < count; ++j) {
      if (i < results[j].size() && results[j][i] == rule.find) {
        std::string copy = results[j];
        copy[i] = rule.replace;
        results.push_back(std::move(copy));
      }
    }

    if (++depth >= rule.levels) break;
  }

  return results;
}

// Apply all rules in sequence to a word
std::vector<std::string> apply_all_rules(const std::string& word, const std::vector<Rule>& rules) {
  std::vector<std::string> current;
  current.reserve(16);
  current.push_back(word);

  for (const auto& rule : rules) {
    std::vector<std::string> next;
    next.reserve(current.size() * 2);

    for (const auto& w : current) {
      auto variants = apply_rule(w, rule);
      next.insert(next.end(), variants.begin(), variants.end());
    }

    // Use unordered_set for faster deduplication
    std::unordered_set<std::string> unique(next.begin(), next.end());
    current.assign(unique.begin(), unique.end());
  }

  return current;
}

// Worker thread function
void worker(const std::vector<std::string>& lines,
            const std::vector<Rule>& rules,
            size_t start, size_t end,
            std::unordered_set<std::string>& global_seen,
            std::mutex& seen_mutex) {
  std::unordered_set<std::string> local_seen;
  local_seen.reserve(1024);

  for (size_t i = start; i < end; ++i) {
    auto results = apply_all_rules(lines[i], rules);
    for (const auto& res : results) {
      local_seen.insert(res);
    }
  }

  std::lock_guard<std::mutex> lock(seen_mutex);
  for (const auto& res : local_seen) {
    if (global_seen.insert(res).second) {
      std::cout << res << '\n';
    }
  }
}

int main(int argc, char* argv[]) {
  std::ios::sync_with_stdio(false);
  std::cout.tie(nullptr);

  if (argc < 2) {
    std::cerr << "Usage: ./aswap 2o0 1e3\n";
    return 1;
  }

  // Parse rules from CLI args
  std::vector<Rule> rules;
  rules.reserve(argc - 1);

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.size() != 3 || !isdigit(arg[0])) {
      std::cerr << "Invalid rule: " << arg << '\n';
      return 1;
    }
    rules.push_back({ arg[0] - '0', arg[1], arg[2] });
  }

  // Read and clean lines from stdin
  std::vector<std::string> lines;
  lines.reserve(100000); // optional tuning for big wordlists

  std::string line;
  while (std::getline(std::cin, line)) {
    if (is_blank(line)) continue;
    std::string cleaned = clean_input(line);
    if (!cleaned.empty()) {
      lines.push_back(std::move(cleaned));
    }
  }

  if (lines.empty()) return 0;

  const size_t n_threads = std::min<size_t>(std::thread::hardware_concurrency(), lines.size());
  if (n_threads <= 1) {
    std::unordered_set<std::string> global_seen;
    std::mutex seen_mutex;
    worker(lines, rules, 0, lines.size(), global_seen, seen_mutex);
    return 0;
  }

  std::unordered_set<std::string> global_seen;
  std::mutex seen_mutex;

  std::vector<std::thread> threads;
  size_t chunk = (lines.size() + n_threads - 1) / n_threads;

  for (size_t i = 0; i < n_threads; ++i) {
    size_t start = i * chunk;
    size_t end = std::min(start + chunk, lines.size());
    if (start < end) {
      threads.emplace_back(worker, std::cref(lines), std::cref(rules),
                           start, end, std::ref(global_seen), std::ref(seen_mutex));
    }
  }

  for (auto& t : threads) t.join();

  return 0;
}

