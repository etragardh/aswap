#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>

// Rule definition
struct Rule {
  int levels;
  char find;
  char replace;
};

// Apply a single rule to a word
std::vector<std::string> apply_rule(const std::string& word, const Rule& rule) {
  std::vector<std::string> results = { word };
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
  std::vector<std::string> current = { word };

  for (const auto& rule : rules) {
    std::vector<std::string> next;
    for (const auto& w : current) {
      auto variants = apply_rule(w, rule);
      next.insert(next.end(), variants.begin(), variants.end());
    }
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    current = std::move(next);
  }

  return current;
}

// Worker thread function
void worker(const std::vector<std::string>& lines,
            const std::vector<Rule>& rules,
            size_t start, size_t end) {
  for (size_t i = start; i < end; ++i) {
    auto results = apply_all_rules(lines[i], rules);
    for (const auto& res : results) {
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

  // Parse rules
  std::vector<Rule> rules;
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.size() != 3 || !isdigit(arg[0])) {
      std::cerr << "Invalid rule: " << arg << '\n';
      return 1;
    }
    rules.push_back({ arg[0] - '0', arg[1], arg[2] });
  }

  // Read all input lines
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty()) lines.push_back(std::move(line));
  }

  // Threading setup
  const size_t n_threads = std::min<size_t>(std::thread::hardware_concurrency(), lines.size());
  if (n_threads <= 1) {
    worker(lines, rules, 0, lines.size());
    return 0;
  }

  std::vector<std::thread> threads;
  size_t chunk = (lines.size() + n_threads - 1) / n_threads;

  for (size_t i = 0; i < n_threads; ++i) {
    size_t start = i * chunk;
    size_t end = std::min(start + chunk, lines.size());
    if (start < end) {
      threads.emplace_back(worker, std::cref(lines), std::cref(rules), start, end);
    }
  }

  for (auto& t : threads) t.join();

  return 0;
}

