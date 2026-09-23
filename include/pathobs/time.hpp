// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Time model.
//
// Path Observatory distinguishes two clocks on purpose:
//   * the observation time a source reports for an event, which lives on the
//     source clock domain and may be skewed or wrong;
//   * the receive time the observatory stamps when the evidence arrives, which
//     lives on the local clock domain.
//
// Freshness decisions are always a pure function of (evidence, now, policy):
// nothing in the classification path reads a wall clock. That is what makes
// classification reproducible and testable.

#ifndef PATHOBS_TIME_HPP
#define PATHOBS_TIME_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace pathobs {

using Nanoseconds = std::chrono::nanoseconds;

/// Absolute instant on the Unix timescale, at nanosecond resolution.
class TimePoint {
 public:
  constexpr TimePoint() noexcept = default;

  static constexpr TimePoint from_unix_nanos(std::int64_t nanos) noexcept {
    TimePoint point;
    point.nanos_ = nanos;
    return point;
  }

  /// The "no value" marker. It is deliberately not the Unix epoch: an evidence
  /// record that carries no observation time must be distinguishable from one
  /// that carries a timestamp of 1970-01-01.
  static constexpr TimePoint unset() noexcept { return TimePoint{}; }

  static Result<TimePoint> parse_rfc3339(std::string_view text);

  constexpr std::int64_t unix_nanos() const noexcept { return nanos_; }
  constexpr bool is_set() const noexcept { return nanos_ != kUnset; }

  std::string to_rfc3339() const;

  friend constexpr bool operator==(const TimePoint&, const TimePoint&) noexcept = default;
  friend constexpr auto operator<=>(const TimePoint&, const TimePoint&) noexcept = default;

 private:
  static constexpr std::int64_t kUnset = INT64_MIN;
  std::int64_t nanos_{kUnset};
};

using Duration = Nanoseconds;

inline constexpr std::int64_t kUnsetTimeNanos = INT64_MIN;

/// Which clock domain a timestamp belongs to.
enum class ClockDomain : std::uint8_t {
  Unknown = 0,
  SourceReported = 1,
  LocalWall = 2,
};

PATHOBS_API const char* to_string(ClockDomain domain) noexcept;

/// Difference between two instants, saturating rather than wrapping.
PATHOBS_API Result<Duration> difference(TimePoint later, TimePoint earlier);

/// True when the instant is later than now by more than the tolerance.
PATHOBS_API bool is_after(TimePoint instant, TimePoint now) noexcept;

class PATHOBS_API Clock {
 public:
  virtual ~Clock();
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;

  virtual TimePoint now() const = 0;

 protected:
  Clock() = default;
};

/// Wall clock source for production use.
class PATHOBS_API SystemClock final : public Clock {
 public:
  SystemClock() = default;
  TimePoint now() const override;
};

/// Deterministic clock for tests, replays and benchmarks. Time advances only
/// when the owner advances it, so classification results never depend on how
/// long a test took to run.
class PATHOBS_API ManualClock final : public Clock {
 public:
  explicit ManualClock(TimePoint start) : current_(start) {}
  TimePoint now() const override { return current_; }

  void advance(Duration delta) {
    current_ = TimePoint::from_unix_nanos(current_.unix_nanos() + delta.count());
  }
  void set(TimePoint value) { current_ = value; }

 private:
  TimePoint current_;
};

} // namespace pathobs

#endif // PATHOBS_TIME_HPP
