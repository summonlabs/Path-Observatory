// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/time.hpp"

#include <array>
#include <cstdio>

namespace pathobs {

const char* to_string(ClockDomain domain) noexcept {
  switch (domain) {
    case ClockDomain::Unknown:
      return "unknown";
    case ClockDomain::SourceReported:
      return "source_reported";
    case ClockDomain::LocalWall:
      return "local_wall";
  }
  return "unknown";
}

Result<Duration> difference(TimePoint later, TimePoint earlier) {
  const std::int64_t a = later.unix_nanos();
  const std::int64_t b = earlier.unix_nanos();
  if (b > 0 && a < INT64_MIN + b) {
    return Error{ErrorCode::OutOfRange, "time difference underflow"};
  }
  if (b < 0 && a > INT64_MAX + b) {
    return Error{ErrorCode::OutOfRange, "time difference overflow"};
  }
  return Duration{a - b};
}

bool is_after(TimePoint instant, TimePoint now) noexcept {
  return instant.unix_nanos() > now.unix_nanos();
}

Clock::~Clock() = default;

TimePoint SystemClock::now() const {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  const auto nanos = std::chrono::duration_cast<Nanoseconds>(since_epoch);
  return TimePoint::from_unix_nanos(nanos.count());
}

namespace {

bool is_leap_year(int year) noexcept {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) noexcept {
  static constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return kDays[static_cast<std::size_t>(month - 1)];
}

/// Days since 1970-01-01 for a proleptic Gregorian date.
std::int64_t days_from_civil(int year, int month, int day) noexcept {
  year -= month <= 2 ? 1 : 0;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const auto year_of_era = static_cast<std::int64_t>(year - era * 400);
  const std::int64_t day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const std::int64_t day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

void civil_from_days(std::int64_t z, int& year, int& month, int& day) noexcept {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t day_of_era = z - era * 146097;
  const std::int64_t year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const std::int64_t y = year_of_era + era * 400;
  const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const std::int64_t mp = (5 * day_of_year + 2) / 153;
  const std::int64_t d = day_of_year - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp + (mp < 10 ? 3 : -9);
  year = static_cast<int>(y + (m <= 2 ? 1 : 0));
  month = static_cast<int>(m);
  day = static_cast<int>(d);
}

bool parse_fixed_int(std::string_view text, std::size_t offset, std::size_t count,
                     int& out) noexcept {
  if (offset + count > text.size()) {
    return false;
  }
  int value = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const char c = text[offset + i];
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

} // namespace

Result<TimePoint> TimePoint::parse_rfc3339(std::string_view text) {
  // Accepted shape: YYYY-MM-DDTHH:MM:SS[.fraction]Z  (UTC only, which is what
  // Path Observatory emits and what it accepts on ingest).
  if (text.size() < 20) {
    return Error{ErrorCode::InvalidFormat, "timestamp is too short for RFC 3339"};
  }
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parse_fixed_int(text, 0, 4, year) || text[4] != '-' || !parse_fixed_int(text, 5, 2, month) ||
      text[7] != '-' || !parse_fixed_int(text, 8, 2, day) ||
      (text[10] != 'T' && text[10] != 't') || !parse_fixed_int(text, 11, 2, hour) ||
      text[13] != ':' || !parse_fixed_int(text, 14, 2, minute) || text[16] != ':' ||
      !parse_fixed_int(text, 17, 2, second)) {
    return Error{ErrorCode::InvalidFormat, "timestamp is not an RFC 3339 instant"};
  }
  if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour > 23 ||
      minute > 59 || second > 60) {
    return Error{ErrorCode::InvalidFormat, "timestamp has an out-of-range field"};
  }

  std::size_t index = 19;
  std::int64_t fraction_nanos = 0;
  if (index < text.size() && text[index] == '.') {
    ++index;
    std::size_t digits = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
      if (digits < 9) {
        fraction_nanos = fraction_nanos * 10 + (text[index] - '0');
        ++digits;
      }
      ++index;
    }
    if (digits == 0) {
      return Error{ErrorCode::InvalidFormat, "timestamp has an empty fraction"};
    }
    for (std::size_t i = digits; i < 9; ++i) {
      fraction_nanos *= 10;
    }
  }
  if (index >= text.size() || (text[index] != 'Z' && text[index] != 'z')) {
    return Error{ErrorCode::InvalidFormat,
                 "timestamp must be UTC (trailing Z); offsets are not accepted"};
  }
  ++index;
  if (index != text.size()) {
    return Error{ErrorCode::InvalidFormat, "timestamp has trailing characters"};
  }

  const std::int64_t days = days_from_civil(year, month, day);
  const std::int64_t seconds =
      days * 86400 + static_cast<std::int64_t>(hour) * 3600 + minute * 60 + second;
  const std::int64_t nanos = seconds * 1000000000 + fraction_nanos;
  return TimePoint::from_unix_nanos(nanos);
}

std::string TimePoint::to_rfc3339() const {
  if (!is_set()) {
    return "unset";
  }
  std::int64_t seconds = nanos_ / 1000000000;
  std::int64_t fraction = nanos_ % 1000000000;
  if (fraction < 0) {
    fraction += 1000000000;
    --seconds;
  }
  std::int64_t days = seconds / 86400;
  std::int64_t remainder = seconds % 86400;
  if (remainder < 0) {
    remainder += 86400;
    --days;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  civil_from_days(days, year, month, day);
  const int hour = static_cast<int>(remainder / 3600);
  const int minute = static_cast<int>((remainder % 3600) / 60);
  const int second = static_cast<int>(remainder % 60);

  char buffer[64]{};
  if (fraction == 0) {
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour,
                  minute, second);
  } else {
    char fractional[16]{};
    std::snprintf(fractional, sizeof(fractional), "%09lld",
                  static_cast<long long>(fraction));
    std::size_t length = 9;
    while (length > 1 && fractional[length - 1] == '0') {
      --length;
    }
    fractional[length] = '\0';
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%sZ", year, month, day,
                  hour, minute, second, fractional);
  }
  return std::string(buffer);
}

} // namespace pathobs
