// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/divergence.hpp"

#include <array>

namespace pathobs {
namespace {

constexpr std::array<DivergenceDescriptor, 26> kDescriptors = {{
    {DivergenceClass::None, "none", DivergenceCategory::None, 0},
    {DivergenceClass::EpochFenced, "epoch_fenced", DivergenceCategory::Fencing, 10},
    {DivergenceClass::EpochAhead, "epoch_ahead", DivergenceCategory::Fencing, 11},
    {DivergenceClass::IncarnationFenced, "incarnation_fenced", DivergenceCategory::Fencing, 12},
    {DivergenceClass::SourceSequenceReplay, "source_sequence_replay", DivergenceCategory::Fencing,
     13},
    {DivergenceClass::RevisionFenced, "revision_fenced", DivergenceCategory::Fencing, 14},
    {DivergenceClass::RevisionAhead, "revision_ahead", DivergenceCategory::Fencing, 15},
    {DivergenceClass::RouteGenerationMismatch, "route_generation_mismatch",
     DivergenceCategory::Generation, 20},
    {DivergenceClass::TopologyGenerationMismatch, "topology_generation_mismatch",
     DivergenceCategory::Generation, 21},
    {DivergenceClass::AuthorityInsufficient, "authority_insufficient",
     DivergenceCategory::Authority, 25},
    {DivergenceClass::EvidenceUnsupported, "evidence_unsupported",
     DivergenceCategory::EvidenceQuality, 30},
    {DivergenceClass::EvidenceExpired, "evidence_expired", DivergenceCategory::EvidenceQuality, 31},
    {DivergenceClass::EvidenceStale, "evidence_stale", DivergenceCategory::EvidenceQuality, 32},
    {DivergenceClass::EvidenceConflicting, "evidence_conflicting", DivergenceCategory::EvidenceQuality,
     40},
    {DivergenceClass::EvidenceIncomplete, "evidence_incomplete", DivergenceCategory::EvidenceQuality,
     41},
    {DivergenceClass::EvidenceAmbiguous, "evidence_ambiguous", DivergenceCategory::EvidenceQuality,
     42},
    {DivergenceClass::EvidenceUnknown, "evidence_unknown", DivergenceCategory::EvidenceQuality, 43},
    {DivergenceClass::ObservationGap, "observation_gap", DivergenceCategory::EvidenceQuality, 44},
    {DivergenceClass::LinkUnresolved, "link_unresolved", DivergenceCategory::EvidenceQuality, 45},
    {DivergenceClass::PathUnknownToPlan, "path_unknown_to_plan", DivergenceCategory::Structural, 50},
    {DivergenceClass::HopMissing, "hop_missing", DivergenceCategory::Structural, 51},
    {DivergenceClass::HopUnexpected, "hop_unexpected", DivergenceCategory::Structural, 52},
    {DivergenceClass::HopOrderViolation, "hop_order_violation", DivergenceCategory::Structural, 53},
    {DivergenceClass::DeviceMismatch, "device_mismatch", DivergenceCategory::Structural, 54},
    {DivergenceClass::PortMismatch, "port_mismatch", DivergenceCategory::Structural, 55},
    {DivergenceClass::LinkMismatch, "link_mismatch", DivergenceCategory::Structural, 56},
}};

} // namespace

const DivergenceDescriptor& describe(DivergenceClass value) noexcept {
  const auto index = static_cast<std::size_t>(value);
  if (index >= kDescriptors.size()) {
    return kDescriptors[0];
  }
  return kDescriptors[index];
}

const char* to_string(DivergenceClass value) noexcept { return describe(value).code; }

const char* to_code(DivergenceClass value) noexcept { return describe(value).code; }

Result<DivergenceClass> parse_divergence_code(std::string_view code) {
  for (const auto& descriptor : kDescriptors) {
    if (code == descriptor.code) {
      return descriptor.value;
    }
  }
  return Error{ErrorCode::InvalidFormat, "unknown divergence code", std::string(code)};
}

const char* to_string(DivergenceCategory category) noexcept {
  switch (category) {
    case DivergenceCategory::None:
      return "none";
    case DivergenceCategory::Fencing:
      return "fencing";
    case DivergenceCategory::Generation:
      return "generation";
    case DivergenceCategory::Authority:
      return "authority";
    case DivergenceCategory::EvidenceQuality:
      return "evidence_quality";
    case DivergenceCategory::Structural:
      return "structural";
  }
  return "none";
}

Result<DivergenceCategory> parse_divergence_category(std::string_view text) {
  if (text == "none") return DivergenceCategory::None;
  if (text == "fencing") return DivergenceCategory::Fencing;
  if (text == "generation") return DivergenceCategory::Generation;
  if (text == "authority") return DivergenceCategory::Authority;
  if (text == "evidence_quality") return DivergenceCategory::EvidenceQuality;
  if (text == "structural") return DivergenceCategory::Structural;
  return Error{ErrorCode::InvalidFormat, "unknown divergence category", std::string(text)};
}

DivergenceCategory category_of(DivergenceClass value) noexcept {
  return describe(value).category;
}

std::uint8_t precedence_of(DivergenceClass value) noexcept {
  return describe(value).precedence;
}

bool indicates_divergence(DivergenceClass value) noexcept {
  return category_of(value) == DivergenceCategory::Structural;
}

bool is_evidence_quality(DivergenceClass value) noexcept {
  const DivergenceCategory category = category_of(value);
  return category == DivergenceCategory::EvidenceQuality || category == DivergenceCategory::Fencing ||
         category == DivergenceCategory::Generation || category == DivergenceCategory::Authority;
}

bool is_fencing(DivergenceClass value) noexcept {
  return category_of(value) == DivergenceCategory::Fencing;
}

DivergenceClass select_primary(const DivergenceClass* classes, std::size_t count) noexcept {
  DivergenceClass best = DivergenceClass::None;
  bool have_best = false;
  for (std::size_t i = 0; i < count; ++i) {
    const DivergenceClass candidate = classes[i];
    if (candidate == DivergenceClass::None) {
      continue;
    }
    if (!have_best) {
      best = candidate;
      have_best = true;
      continue;
    }
    const std::uint8_t candidate_precedence = precedence_of(candidate);
    const std::uint8_t best_precedence = precedence_of(best);
    if (candidate_precedence < best_precedence ||
        (candidate_precedence == best_precedence &&
         static_cast<std::uint8_t>(candidate) < static_cast<std::uint8_t>(best))) {
      best = candidate;
    }
  }
  return best;
}

const DivergenceDescriptor* all_divergence_descriptors(std::size_t* count) noexcept {
  if (count != nullptr) {
    *count = kDescriptors.size();
  }
  return kDescriptors.data();
}

const char* to_string(MatchOutcome outcome) noexcept {
  switch (outcome) {
    case MatchOutcome::NoExpectedState:
      return "no_expected_state";
    case MatchOutcome::Match:
      return "match";
    case MatchOutcome::Divergent:
      return "divergent";
    case MatchOutcome::Indeterminate:
      return "indeterminate";
    case MatchOutcome::Unsupported:
      return "unsupported";
  }
  return "indeterminate";
}

Result<MatchOutcome> parse_match_outcome(std::string_view text) {
  if (text == "no_expected_state") return MatchOutcome::NoExpectedState;
  if (text == "match") return MatchOutcome::Match;
  if (text == "divergent") return MatchOutcome::Divergent;
  if (text == "indeterminate") return MatchOutcome::Indeterminate;
  if (text == "unsupported") return MatchOutcome::Unsupported;
  return Error{ErrorCode::InvalidFormat, "unknown match outcome", std::string(text)};
}

bool is_compliance(MatchOutcome outcome) noexcept { return outcome == MatchOutcome::Match; }

const char* to_string(ObservationConsistency consistency) noexcept {
  switch (consistency) {
    case ObservationConsistency::Unknown:
      return "unknown";
    case ObservationConsistency::Consistent:
      return "consistent";
    case ObservationConsistency::Ambiguous:
      return "ambiguous";
    case ObservationConsistency::Conflicting:
      return "conflicting";
    case ObservationConsistency::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

Result<ObservationConsistency> parse_observation_consistency(std::string_view text) {
  if (text == "unknown") return ObservationConsistency::Unknown;
  if (text == "consistent") return ObservationConsistency::Consistent;
  if (text == "ambiguous") return ObservationConsistency::Ambiguous;
  if (text == "conflicting") return ObservationConsistency::Conflicting;
  if (text == "unsupported") return ObservationConsistency::Unsupported;
  return Error{ErrorCode::InvalidFormat, "unknown observation consistency", std::string(text)};
}

} // namespace pathobs
