// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/freshness.hpp"

namespace pathobs {

const char* to_string(Freshness freshness) noexcept {
  switch (freshness) {
    case Freshness::Unknown:
      return "unknown";
    case Freshness::Fresh:
      return "fresh";
    case Freshness::Stale:
      return "stale";
    case Freshness::Expired:
      return "expired";
    case Freshness::Future:
      return "future";
  }
  return "unknown";
}

Result<Freshness> parse_freshness(std::string_view text) {
  if (text == "unknown") return Freshness::Unknown;
  if (text == "fresh") return Freshness::Fresh;
  if (text == "stale") return Freshness::Stale;
  if (text == "expired") return Freshness::Expired;
  if (text == "future") return Freshness::Future;
  return Error{ErrorCode::InvalidFormat, "unknown freshness value", std::string(text)};
}

std::uint8_t freshness_severity(Freshness freshness) noexcept {
  switch (freshness) {
    case Freshness::Fresh:
      return 0;
    case Freshness::Unknown:
      return 1;
    case Freshness::Stale:
      return 2;
    case Freshness::Expired:
      return 3;
    case Freshness::Future:
      return 4;
  }
  return 4;
}

Freshness worse_of(Freshness a, Freshness b) noexcept {
  return freshness_severity(a) >= freshness_severity(b) ? a : b;
}

bool is_compliance_capable(Freshness freshness) noexcept {
  return freshness == Freshness::Fresh;
}

Result<void> FreshnessPolicy::validate() const {
  if (evidence_max_age.count() < 0 || evidence_expiry.count() < 0 ||
      generation_max_age.count() < 0 || generation_expiry.count() < 0 ||
      clock_skew_tolerance.count() < 0 || max_observation_latency.count() < 0) {
    return Error{ErrorCode::InvalidArgument, "freshness policy durations must not be negative"};
  }
  if (evidence_expiry < evidence_max_age) {
    return Error{ErrorCode::InvalidArgument,
                 "evidence expiry must not be shorter than the evidence freshness horizon"};
  }
  if (generation_expiry < generation_max_age) {
    return Error{ErrorCode::InvalidArgument,
                 "generation expiry must not be shorter than the generation freshness horizon"};
  }
  return ok();
}

namespace {

/// Classify an age against a freshness horizon and an expiry horizon.
Freshness classify_age(Duration age, Duration max_age, Duration expiry) {
  if (age <= max_age) {
    return Freshness::Fresh;
  }
  if (age <= expiry) {
    return Freshness::Stale;
  }
  return Freshness::Expired;
}

} // namespace

FreshnessAssessment assess_evidence(const ObservedPathEvidence& evidence, TimePoint now,
                                    const FreshnessPolicy& policy) {
  FreshnessAssessment assessment;

  if (!now.is_set()) {
    assessment.state = Freshness::Unknown;
    assessment.reason = "no evaluation time was supplied";
    return assessment;
  }
  if (!evidence.received_at.is_set()) {
    assessment.state = Freshness::Unknown;
    assessment.reason = "evidence carries no receive time";
    return assessment;
  }

  const std::int64_t age_nanos = now.unix_nanos() - evidence.received_at.unix_nanos();
  assessment.age_known = true;
  assessment.age = Duration{age_nanos};

  if (age_nanos < 0) {
    const Duration ahead{-age_nanos};
    if (ahead > policy.clock_skew_tolerance) {
      assessment.state = Freshness::Future;
      assessment.skew = ahead;
      assessment.skew_detected = true;
      assessment.reason = "receive time lies ahead of the evaluation time";
      return assessment;
    }
    // Within tolerance the clock difference is not material.
    assessment.state = Freshness::Fresh;
    assessment.age = Duration{0};
    assessment.reason = "received within the clock skew tolerance of the evaluation time";
  } else {
    assessment.state =
        classify_age(assessment.age, policy.evidence_max_age, policy.evidence_expiry);
    switch (assessment.state) {
      case Freshness::Fresh:
        assessment.reason = "received inside the evidence freshness horizon";
        break;
      case Freshness::Stale:
        assessment.reason = "received outside the evidence freshness horizon";
        break;
      default:
        assessment.reason = "received outside the evidence expiry horizon";
        break;
    }
  }

  if (evidence.observed_at.is_set()) {
    const std::int64_t skew_nanos =
        evidence.observed_at.unix_nanos() - evidence.received_at.unix_nanos();
    assessment.skew = Duration{skew_nanos};
    if (skew_nanos > policy.clock_skew_tolerance.count()) {
      assessment.skew_detected = true;
      assessment.state = worse_of(assessment.state, Freshness::Future);
      assessment.reason = "source observation time is ahead of the receive time beyond tolerance";
    } else if (-skew_nanos > policy.max_observation_latency.count()) {
      assessment.skew_detected = true;
      // A source clock that lags by more than the tolerated latency means the
      // observatory cannot place the observation in time. That is a judgement
      // failure, not a freshness verdict, so it degrades to Unknown.
      assessment.state = worse_of(assessment.state, Freshness::Unknown);
      assessment.reason = "source observation time lags the receive time beyond the tolerated "
                          "latency";
    }
  }

  return assessment;
}

FreshnessAssessment assess_generation(TimePoint created, TimePoint received, TimePoint now,
                                      const FreshnessPolicy& policy) {
  FreshnessAssessment assessment;
  if (!now.is_set()) {
    assessment.state = Freshness::Unknown;
    assessment.reason = "no evaluation time was supplied";
    return assessment;
  }
  const TimePoint anchor = received.is_set() ? received : created;
  if (!anchor.is_set()) {
    assessment.state = Freshness::Unknown;
    assessment.reason = "generation carries no timestamp";
    return assessment;
  }
  const std::int64_t age_nanos = now.unix_nanos() - anchor.unix_nanos();
  assessment.age_known = true;
  assessment.age = Duration{age_nanos};
  if (age_nanos < 0) {
    const Duration ahead{-age_nanos};
    if (ahead > policy.clock_skew_tolerance) {
      assessment.state = Freshness::Future;
      assessment.skew = ahead;
      assessment.skew_detected = true;
      assessment.reason = "generation timestamp lies ahead of the evaluation time";
      return assessment;
    }
    assessment.state = Freshness::Fresh;
    assessment.age = Duration{0};
    assessment.reason = "generation timestamp is within the clock skew tolerance";
    return assessment;
  }
  assessment.state =
      classify_age(assessment.age, policy.generation_max_age, policy.generation_expiry);
  switch (assessment.state) {
    case Freshness::Fresh:
      assessment.reason = "generation is inside the freshness horizon";
      break;
    case Freshness::Stale:
      assessment.reason = "generation is outside the freshness horizon";
      break;
    default:
      assessment.reason = "generation is outside the expiry horizon";
      break;
  }
  return assessment;
}

} // namespace pathobs
