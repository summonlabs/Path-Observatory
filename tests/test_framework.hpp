// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A deliberately small test framework.
//
// There are no timeouts anywhere in this suite. A test that waits uses a
// condition the runtime is required to satisfy; if the runtime fails to satisfy
// it the suite hangs, which is the honest outcome for a liveness defect. A
// timeout would convert a real defect into an intermittent pass.

#ifndef PATHOBS_TEST_FRAMEWORK_HPP
#define PATHOBS_TEST_FRAMEWORK_HPP

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace pathobs::testing {

using Body = void (*)();

struct TestCase {
  std::string suite;
  std::string name;
  Body body;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* suite, const char* name, Body body) {
    registry().push_back(TestCase{suite, name, body});
  }
};

/// Shared state for the currently running test.
struct RunState {
  int failures{0};
  std::string current;
  std::vector<std::string> messages;
};

RunState& state();

void fail(const char* file, int line, const std::string& message);

inline void check(bool condition, const char* expression, const char* file, int line) {
  if (!condition) {
    fail(file, line, std::string("condition failed: ") + expression);
  }
}

template <class A, class B>
void check_equal(const A& left, const B& right, const char* left_text, const char* right_text,
                 const char* file, int line) {
  if (!(left == right)) {
    fail(file, line, std::string("expected ") + left_text + " == " + right_text);
  }
}

template <class A, class B>
void check_not_equal(const A& left, const B& right, const char* left_text, const char* right_text,
                     const char* file, int line) {
  if (left == right) {
    fail(file, line, std::string("expected ") + left_text + " != " + right_text);
  }
}

int run_all(int argc, char** argv);

} // namespace pathobs::testing

#define PATHOBS_TEST(suite_name, test_name)                                        \
  static void suite_name##_##test_name();                                          \
  static const ::pathobs::testing::Registrar pathobs_registrar_##suite_name##_##test_name( \
      #suite_name, #test_name, &suite_name##_##test_name);                         \
  static void suite_name##_##test_name()

#define CHECK(expression)   ::pathobs::testing::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CHECK_EQ(left, right)                                                          \
  ::pathobs::testing::check_equal((left), (right), #left, #right, __FILE__, __LINE__)

#define CHECK_NE(left, right)                                                             \
  ::pathobs::testing::check_not_equal((left), (right), #left, #right, __FILE__, __LINE__)

/// Record a failure with a message the test composes, for the cases where the
/// useful information is an error string rather than an expression.
#define FAIL_WITH(message) ::pathobs::testing::fail(__FILE__, __LINE__, (message))

#define REQUIRE(expression)                     \
  do {                                          \
    if (!(expression)) {                        \
      ::pathobs::testing::fail(__FILE__, __LINE__, \
                               "required: " #expression); \
      return;                                   \
    }                                           \
  } while (false)

#endif // PATHOBS_TEST_FRAMEWORK_HPP
