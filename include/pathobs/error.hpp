// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Error and result vocabulary.
//
// Every fallible operation in Path Observatory reports failure through Result.
// The runtime deliberately avoids exceptions for domain failures so that a
// caller cannot silently ignore an integrity problem: the compiler forces the
// caller to look at the result.

#ifndef PATHOBS_ERROR_HPP
#define PATHOBS_ERROR_HPP

#include "pathobs/export.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace pathobs {

enum class ErrorCode : std::uint16_t {
  None = 0,

  // Generic argument / shape problems.
  InvalidArgument,
  OutOfRange,
  CapacityExceeded,
  NotSupported,
  Internal,

  // Identity and reference problems.
  InvalidIdentity,
  UnknownIdentity,
  DuplicateIdentity,

  // Encoding problems.
  InvalidFormat,
  TruncatedInput,
  IntegrityFailure,
  UnsupportedVersion,

  // Domain problems.
  UnknownGeneration,
  FencedEvidence,
  InsufficientEvidence,

  // Persistence problems.
  IoFailure,
  CorruptStore,
  ReadOnlyStore,

  // Transport problems.
  TransportFailure,
  ProtocolViolation,
  ConnectionClosed,

  // Lifecycle problems.
  InvalidState,
  Cancelled,
  ShuttingDown,
};

/// Stable, machine readable spelling of an error code. Never localised and
/// never renumbered: the numeric value is part of the reporting contract.
PATHOBS_API const char* to_string(ErrorCode code) noexcept;

struct Error {
  ErrorCode code{ErrorCode::None};
  std::string message;
  std::string detail;

  Error() = default;
  Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
  Error(ErrorCode c, std::string msg, std::string det)
      : code(c), message(std::move(msg)), detail(std::move(det)) {}

  friend bool operator==(const Error&, const Error&) noexcept = default;

  std::string to_string() const;
};

/// Single line rendering: "code: message (detail)".
PATHOBS_API std::string describe(const Error& error);

template <class T>
class [[nodiscard]] Result {
 public:
  using value_type = T;

  Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
  Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

  bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() & { return std::get<0>(storage_); }
  const T& value() const& { return std::get<0>(storage_); }
  T&& value() && { return std::get<0>(std::move(storage_)); }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  const Error& error() const& { return std::get<1>(storage_); }

  T value_or(T fallback) const {
    return has_value() ? value() : std::move(fallback);
  }

 private:
  std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  using value_type = void;

  Result() = default;
  Result(Error error) : error_(std::move(error)) {}

  bool has_value() const noexcept { return !error_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  const Error& error() const& { return *error_; }

 private:
  std::optional<Error> error_{};
};

using Status = Result<void>;

inline Status ok() noexcept { return Status{}; }

template <class T>
inline Status failure(ErrorCode code, std::string message) {
  return Status{Error{code, std::move(message)}};
}

} // namespace pathobs

// Propagate the error of an expression that yields a Result. The declaration is
// PATHOBS_TRY(name, expr); on failure the enclosing function returns the same
// error immediately.
// The two level indirection is required: an operand of the token pasting
// operator is not macro expanded, so __COUNTER__ has to be expanded by the
// outer macro before the inner macro pastes it.
#define PATHOBS_TRY_PASTE(name, expr, counter)                    \
  auto&& pathobs_try_result_##counter = (expr);                   \
  if (!pathobs_try_result_##counter.has_value()) {                \
    return pathobs_try_result_##counter.error();                  \
  }                                                               \
  auto&& name = std::move(pathobs_try_result_##counter).value()

#define PATHOBS_TRY_IMPL(name, expr, counter) PATHOBS_TRY_PASTE(name, expr, counter)

#define PATHOBS_TRY(name, expr) PATHOBS_TRY_IMPL(name, expr, __COUNTER__)

// Assign a name from a Result, returning the error when it is not a value.
#define PATHOBS_ASSIGN_PASTE(name, expr, counter)                 \
  auto&& pathobs_assign_result_##counter = (expr);                \
  if (!pathobs_assign_result_##counter.has_value()) {             \
    return pathobs_assign_result_##counter.error();               \
  }                                                               \
  name = std::move(pathobs_assign_result_##counter).value()

#define PATHOBS_ASSIGN_IMPL(name, expr, counter) PATHOBS_ASSIGN_PASTE(name, expr, counter)

#define PATHOBS_ASSIGN(name, expr) PATHOBS_ASSIGN_IMPL(name, expr, __COUNTER__)

#endif // PATHOBS_ERROR_HPP
