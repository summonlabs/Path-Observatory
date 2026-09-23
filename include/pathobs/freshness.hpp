// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Freshness.
//
// Freshness is always evaluated against an explicitly supplied evaluation time.
// Nothing in this file reads a clock, which is what makes a classification
// reproducible and what makes a persisted observation stay stale after a
// restart instead of silently becoming fresh again.

#ifndef PATHOBS_FRESHNESS_HPP
#define PATHOBS_FRESHNESS_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/time.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace pathobs {

enum class Freshness : std::uint8_t {
  /// The timestamps do not permit a judgement. Never treated as fresh.
  Unknown = 0,
  Fresh = 1,
  /// Older than the freshness horizon but still inside the expiry horizon.
  Stale = 2,
  /// Older than the expiry horizon.
  Expired = 3,
  /// A timestamp lies ahead of the evaluation time beyond the skew tolerance.
  Future = 4,
};

PATHOBS_API const char* to_string(Freshness freshness) noexcept;
PATHOBS_API Result<Freshness> parse_freshness(std::string_view text);

/// Severity ordering used when two freshness judgements have to be combined:
/// the worse of the two wins. Fresh is the only value that can ever support a
/// compliance statement.
PATHOBS_API std::uint8_t freshness_severity(Freshness freshness) noexcept;
PATHOBS_API Freshness worse_of(Freshness a, Freshness b) noexcept;
PATHOBS_API bool is_compliance_capable(Freshness freshness) noexcept;

struct FreshnessPolicy {
  /// Age (measured from the receive time) up to which evidence is fresh.
  Duration evidence_max_age{std::chrono::seconds(30)};
  /// Age beyond which evidence is expired rather than merely stale.
  Duration evidence_expiry{std::chrono::minutes(10)};
  /// Age of a consumed expected-state generation up to which it is fresh.
  Duration generation_max_age{std::chrono::minutes(5)};
  Duration generation_expiry{std::chrono::minutes(30)};
  /// Difference tolerated between a source timestamp and a local timestamp
  /// before the source clock is treated as skewed.
  Duration clock_skew_tolerance{std::chrono::seconds(1)};
  /// Largest tolerated delay between a source observing an event and the
  /// observatory receiving it before the observation is treated as suspect.
  Duration max_observation_latency{std::chrono::seconds(5)};

  Result<void> validate() const;
};

struct FreshnessAssessment {
  Freshness state{Freshness::Unknown};
  Duration age{0};
  Duration skew{0};
  bool skew_detected{false};
  bool age_known{false};
  std::string reason;

  friend bool operator==(const FreshnessAssessment&, const FreshnessAssessment&) noexcept = default;
};

/// Assess one evidence record against an evaluation time.
PATHOBS_API FreshnessAssessment assess_evidence(const ObservedPathEvidence& evidence, TimePoint now,
                                                const FreshnessPolicy& policy);

/// Assess a consumed expected-state generation against an evaluation time.
PATHOBS_API FreshnessAssessment assess_generation(TimePoint created, TimePoint received,
                                                  TimePoint now, const FreshnessPolicy& policy);

} // namespace pathobs

#endif // PATHOBS_FRESHNESS_HPP
