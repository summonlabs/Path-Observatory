// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Checked arithmetic. Every size that is derived from external input (a file, a
// frame, a payload length field, a count field in a routing document) flows
// through these helpers before it is used to size an allocation, index a
// buffer, or bound a loop. Overflow is an error, never a wrap.

#ifndef PATHOBS_CHECKED_HPP
#define PATHOBS_CHECKED_HPP

#include "pathobs/error.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace pathobs {

/// Explicit narrowing conversion. The cast is intentional and reviewed at each
/// call site; the helper exists so that a toolchain configured with /W4 does
/// not drown real findings in implicit-conversion noise.
template <class To, class From>
constexpr To narrow_cast(From value) noexcept {
  return static_cast<To>(value);
}

template <class T>
inline Result<T> checked_add(T a, T b) {
  static_assert(std::is_integral_v<T>, "checked_add requires an integral type");
  if constexpr (std::is_unsigned_v<T>) {
    if (b > static_cast<T>(std::numeric_limits<T>::max() - a)) {
      return Error{ErrorCode::OutOfRange, "unsigned addition overflow"};
    }
  } else {
    if (b > 0 && a > static_cast<T>(std::numeric_limits<T>::max() - b)) {
      return Error{ErrorCode::OutOfRange, "signed addition overflow"};
    }
    if (b < 0 && a < static_cast<T>(std::numeric_limits<T>::min() - b)) {
      return Error{ErrorCode::OutOfRange, "signed addition underflow"};
    }
  }
  return static_cast<T>(a + b);
}

template <class T>
inline Result<T> checked_sub(T a, T b) {
  static_assert(std::is_integral_v<T>, "checked_sub requires an integral type");
  if constexpr (std::is_unsigned_v<T>) {
    if (b > a) {
      return Error{ErrorCode::OutOfRange, "unsigned subtraction underflow"};
    }
  } else {
    if (b < 0 && a > static_cast<T>(std::numeric_limits<T>::max() + b)) {
      return Error{ErrorCode::OutOfRange, "signed subtraction overflow"};
    }
    if (b > 0 && a < static_cast<T>(std::numeric_limits<T>::min() + b)) {
      return Error{ErrorCode::OutOfRange, "signed subtraction underflow"};
    }
  }
  return static_cast<T>(a - b);
}

template <class T>
inline Result<T> checked_mul(T a, T b) {
  static_assert(std::is_integral_v<T>, "checked_mul requires an integral type");
  if (a == 0 || b == 0) {
    return static_cast<T>(0);
  }
  if constexpr (std::is_unsigned_v<T>) {
    if (a > static_cast<T>(std::numeric_limits<T>::max() / b)) {
      return Error{ErrorCode::OutOfRange, "unsigned multiplication overflow"};
    }
    return static_cast<T>(a * b);
  } else {
    if (a == std::numeric_limits<T>::min() || b == std::numeric_limits<T>::min()) {
      return Error{ErrorCode::OutOfRange, "signed multiplication overflow"};
    }
    const T abs_a = a < 0 ? static_cast<T>(-a) : a;
    const T abs_b = b < 0 ? static_cast<T>(-b) : b;
    const T magnitude_limit = std::numeric_limits<T>::max();
    if (abs_a > static_cast<T>(magnitude_limit / abs_b)) {
      return Error{ErrorCode::OutOfRange, "signed multiplication overflow"};
    }
    const T magnitude = static_cast<T>(abs_a * abs_b);
    const bool negative = (a < 0) != (b < 0);
    return negative ? static_cast<T>(-magnitude) : magnitude;
  }
}

/// Convert between integral types, failing when the value does not fit.
template <class To, class From>
inline Result<To> checked_cast(From value) {
  static_assert(std::is_integral_v<To> && std::is_integral_v<From>,
                "checked_cast requires integral types");
  if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>) {
    if (value < 0) {
      return Error{ErrorCode::OutOfRange, "negative value cannot convert to unsigned"};
    }
  }
  using CommonTo = std::common_type_t<To, From>;
  const auto wide = static_cast<CommonTo>(value);
  if (wide > static_cast<CommonTo>(std::numeric_limits<To>::max())) {
    return Error{ErrorCode::OutOfRange, "value exceeds destination range"};
  }
  if constexpr (std::is_signed_v<To>) {
    if (wide < static_cast<CommonTo>(std::numeric_limits<To>::min())) {
      return Error{ErrorCode::OutOfRange, "value below destination range"};
    }
  }
  return static_cast<To>(value);
}

/// True when a count of elements of the given size fits within a byte limit.
inline bool fits_within(std::uint64_t count, std::uint64_t element_size,
                        std::uint64_t limit) noexcept {
  if (element_size != 0 && count > limit / element_size) {
    return false;
  }
  return count * element_size <= limit;
}

} // namespace pathobs

#endif // PATHOBS_CHECKED_HPP
