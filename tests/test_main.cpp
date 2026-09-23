// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "test_framework.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pathobs::testing {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

RunState& state() {
  static RunState current;
  return current;
}

void fail(const char* file, int line, const std::string& message) {
  RunState& current = state();
  ++current.failures;
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), ":%d", line);
  current.messages.push_back(std::string(file) + buffer + ": " + message);
}

int run_all(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      filter = argv[++i];
    }
  }

  std::vector<TestCase> cases = registry();
  // Registration order across translation units is unspecified; sorting makes
  // the run order identical on every platform and every build.
  std::sort(cases.begin(), cases.end(), [](const TestCase& a, const TestCase& b) {
    if (a.suite != b.suite) {
      return a.suite < b.suite;
    }
    return a.name < b.name;
  });

  int executed = 0;
  int failed = 0;
  for (const auto& test : cases) {
    const std::string full = test.suite + "." + test.name;
    if (!filter.empty() && full.find(filter) == std::string::npos) {
      continue;
    }
    RunState& current = state();
    current.failures = 0;
    current.messages.clear();
    current.current = full;
    test.body();
    ++executed;
    if (current.failures == 0) {
      std::printf("  ok    %s\n", full.c_str());
    } else {
      ++failed;
      std::printf("  FAIL  %s\n", full.c_str());
      for (const auto& message : current.messages) {
        std::printf("          %s\n", message.c_str());
      }
    }
    std::fflush(stdout);
  }

  std::printf("%d test(s) executed, %d failed\n", executed, failed);
  if (executed == 0) {
    std::printf("no test matched the filter\n");
    return 1;
  }
  return failed == 0 ? 0 : 1;
}

} // namespace pathobs::testing

int main(int argc, char** argv) { return pathobs::testing::run_all(argc, argv); }
