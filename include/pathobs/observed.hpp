// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Observed path evidence.
//
// An evidence record states, completely and explicitly:
//   * what was observed (the hop trace, and how far it goes),
//   * which source observed it, under which incarnation and sequence,
//   * which generation, epoch, incarnation and revision the source believes it
//     was observing,
//   * when the source says it observed it and when the observatory received it,
//   * the proof surface the evidence belongs to.
//
// Evidence identity is content derived and deliberately excludes receive time:
// the same observation replayed later is the same evidence, which is what makes
// replay detectable by sequence and incarnation rather than by identity.

#ifndef PATHOBS_OBSERVED_HPP
#define PATHOBS_OBSERVED_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/source.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <string>
#include <vector>

namespace pathobs {

/// How the link identity attached to an observed hop was obtained.
enum class HopLinkProvenance : std::uint8_t {
  /// The source did not report a link and the observatory could not resolve one.
  Absent = 0,
  /// The source reported the link identity directly.
  Observed = 1,
  /// The observatory resolved the link from the topology generation.
  Correlated = 2,
};

PATHOBS_API const char* to_string(HopLinkProvenance provenance) noexcept;
PATHOBS_API Result<HopLinkProvenance> parse_hop_link_provenance(std::string_view text);

/// How complete the observation claims to be.
enum class EvidenceCompleteness : std::uint8_t {
  /// The source did not say, and the observatory could not infer it.
  Unknown = 0,
  /// The hop trace covers part of the path only.
  Partial = 1,
  /// The hop trace covers the whole path.
  Complete = 2,
};

PATHOBS_API const char* to_string(EvidenceCompleteness completeness) noexcept;
PATHOBS_API Result<EvidenceCompleteness> parse_evidence_completeness(std::string_view text);

struct ObservedHop {
  std::uint32_t index{0};
  DeviceId device{};
  PortId ingress{};
  PortId egress{};
  LinkId link{};
  HopLinkProvenance link_provenance{HopLinkProvenance::Absent};
  TimePoint at{};

  friend bool operator==(const ObservedHop&, const ObservedHop&) noexcept = default;
};

struct ObservedPathEvidence {
  EvidenceId id{};
  SourceId source{};
  SourceIncarnationId incarnation{};
  SourceSequence sequence{};
  ProofSurface surface{ProofSurface::Unknown};
  AuthorityClass authority{AuthorityClass::None};

  MaybeId<PathId> path{};
  MaybeId<PathGroupId> group{};
  MaybeId<DeviceId> origin{};
  MaybeId<DeviceId> destination{};

  EpochId epoch{};
  IncarnationId claimed_incarnation{};
  MaybeId<RouteGenerationId> route_generation{};
  MaybeId<TopologyGenerationId> topology_generation{};
  RevisionId revision{};

  TimePoint observed_at{};
  TimePoint received_at{};

  std::vector<ObservedHop> hops;
  bool source_truncated{false};
  EvidenceCompleteness declared_completeness{EvidenceCompleteness::Unknown};

  /// Digest of the raw payload the source supplied, retained so that a
  /// comparison can name the exact bytes it judged.
  Digest payload_digest{};
  std::string note;

  Result<void> validate(const Limits& limits) const;

  /// Canonical encoding, excluding receive time by design.
  void write_canonical(CanonicalWriter& writer) const;
  Digest canonical_digest() const;

  /// Recompute id from content. Always call after mutating a record.
  void seal();

  /// The most complete statement this record makes about itself. It is a claim,
  /// not a fact: the comparison engine re-derives completeness from the trace.
  EvidenceCompleteness claimed_completeness() const noexcept;
};

/// True when the hop indices form a contiguous ascending run starting at zero.
PATHOBS_API bool hop_indices_are_contiguous(const std::vector<ObservedHop>& hops);

/// Evidence identity: a digest over the canonical encoding of the record.
PATHOBS_API EvidenceId compute_evidence_id(const ObservedPathEvidence& evidence);

} // namespace pathobs

#endif // PATHOBS_OBSERVED_HPP
