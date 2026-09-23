// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Divergence vocabulary.
//
// The class list is closed, versioned and ordered. Every class has a stable
// machine readable code that never changes meaning, a category that says
// whether it describes observed behaviour or the quality of the evidence, and a
// precedence that makes the primary class of a comparison deterministic when
// several classes apply at once.

#ifndef PATHOBS_DIVERGENCE_HPP
#define PATHOBS_DIVERGENCE_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pathobs {

enum class DivergenceClass : std::uint8_t {
  /// No divergence, no evidence problem.
  None = 0,

  // --- fencing: the evidence cannot be used against current behaviour -------
  EpochFenced = 1,
  EpochAhead = 2,
  IncarnationFenced = 3,
  SourceSequenceReplay = 4,
  RevisionFenced = 5,
  RevisionAhead = 6,
  RouteGenerationMismatch = 7,
  TopologyGenerationMismatch = 8,

  // --- authority -----------------------------------------------------------
  AuthorityInsufficient = 9,

  // --- evidence quality ----------------------------------------------------
  EvidenceUnsupported = 10,
  EvidenceExpired = 11,
  EvidenceStale = 12,
  EvidenceConflicting = 13,
  EvidenceIncomplete = 14,
  EvidenceAmbiguous = 15,
  EvidenceUnknown = 16,
  ObservationGap = 17,
  LinkUnresolved = 18,

  // --- structural divergence ----------------------------------------------
  PathUnknownToPlan = 19,
  HopMissing = 20,
  HopUnexpected = 21,
  HopOrderViolation = 22,
  DeviceMismatch = 23,
  PortMismatch = 24,
  LinkMismatch = 25,
};

enum class DivergenceCategory : std::uint8_t {
  None = 0,
  /// The evidence is fenced: it cannot speak about current behaviour at all.
  Fencing = 1,
  /// The generation or authority relationship is wrong.
  Generation = 2,
  /// The source is not allowed to support the conclusion.
  Authority = 3,
  /// The evidence exists but cannot support a conclusion.
  EvidenceQuality = 4,
  /// The observation contradicts the plan.
  Structural = 5,
};

struct DivergenceDescriptor {
  DivergenceClass value{DivergenceClass::None};
  const char* code{""};
  DivergenceCategory category{DivergenceCategory::None};
  /// Lower precedence wins when several classes apply. The order is fixed and
  /// is part of the reporting contract.
  std::uint8_t precedence{0};
};

PATHOBS_API const DivergenceDescriptor& describe(DivergenceClass value) noexcept;
PATHOBS_API const char* to_string(DivergenceClass value) noexcept;
PATHOBS_API const char* to_code(DivergenceClass value) noexcept;
PATHOBS_API Result<DivergenceClass> parse_divergence_code(std::string_view code);
PATHOBS_API const char* to_string(DivergenceCategory category) noexcept;
PATHOBS_API Result<DivergenceCategory> parse_divergence_category(std::string_view text);

PATHOBS_API DivergenceCategory category_of(DivergenceClass value) noexcept;
PATHOBS_API std::uint8_t precedence_of(DivergenceClass value) noexcept;

/// True when the class states that observed behaviour contradicts the plan.
PATHOBS_API bool indicates_divergence(DivergenceClass value) noexcept;

/// True when the class describes a problem with the evidence itself.
PATHOBS_API bool is_evidence_quality(DivergenceClass value) noexcept;

/// True when the class fences evidence out of consideration entirely.
PATHOBS_API bool is_fencing(DivergenceClass value) noexcept;

/// Pick the primary class from a set: lowest precedence, ties broken by the
/// enum value so the result never depends on iteration order.
PATHOBS_API DivergenceClass select_primary(const DivergenceClass* classes, std::size_t count) noexcept;

/// Every class, in precedence order. Used by tooling and by the self test.
PATHOBS_API const DivergenceDescriptor* all_divergence_descriptors(std::size_t* count) noexcept;

/// Result of comparing observed behaviour against the plan.
enum class MatchOutcome : std::uint8_t {
  /// The plan has nothing to say about the subject of the comparison.
  NoExpectedState = 0,
  /// Observed behaviour agrees with the plan, on fresh, complete, consistent,
  /// sufficiently authoritative evidence.
  Match = 1,
  /// Observed behaviour contradicts the plan.
  Divergent = 2,
  /// The evidence cannot support either conclusion.
  Indeterminate = 3,
  /// The evidence belongs to a surface this build cannot judge.
  Unsupported = 4,
};

PATHOBS_API const char* to_string(MatchOutcome outcome) noexcept;
PATHOBS_API Result<MatchOutcome> parse_match_outcome(std::string_view text);
/// True only for Match: the sole outcome that may be described as compliance.
PATHOBS_API bool is_compliance(MatchOutcome outcome) noexcept;

/// Agreement between the accepted evidence records of one comparison.
enum class ObservationConsistency : std::uint8_t {
  Unknown = 0,
  /// All accepted records describe the same trace.
  Consistent = 1,
  /// The records describe different, individually legitimate traces (multipath).
  Ambiguous = 2,
  /// The records contradict each other.
  Conflicting = 3,
  /// The records belong to a surface that cannot be judged.
  Unsupported = 4,
};

PATHOBS_API const char* to_string(ObservationConsistency consistency) noexcept;
PATHOBS_API Result<ObservationConsistency> parse_observation_consistency(std::string_view text);

} // namespace pathobs

#endif // PATHOBS_DIVERGENCE_HPP
