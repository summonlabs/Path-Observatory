// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The comparison engine.
//
// The engine is a pure function: it reads no clock, performs no I/O, mutates
// nothing outside its own result, and its output is a deterministic function of
// (request, context, policy). That property is what makes the classification
// reproducible under reordered or conflicting evidence, and it is enforced by
// tests that permute the input and compare canonical digests.
//
// The engine never computes a route, never chooses an alternate, and never
// mutates forwarding. It answers exactly one question: does the observed
// behaviour, as evidenced by these records, agree with what the routing
// authority declared for this generation?

#ifndef PATHOBS_COMPARE_HPP
#define PATHOBS_COMPARE_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/divergence.hpp"
#include "pathobs/error.hpp"
#include "pathobs/expected.hpp"
#include "pathobs/export.hpp"
#include "pathobs/freshness.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/source.hpp"
#include "pathobs/topology.hpp"

#include <string>
#include <vector>

namespace pathobs {

struct ComparisonPolicy {
  FreshnessPolicy freshness{};

  /// An observation whose authority class is below Advisory cannot, on its own,
  /// support a compliance statement. Default true: authority is explicit and
  /// never assumed.
  bool require_advisory_or_higher{true};

  /// When true only Real proof surface evidence can produce a Match. The
  /// default is false so that synthetic evidence stays useful in laboratories,
  /// and every result carries its surface so the caller cannot mistake it.
  bool require_real_surface_for_match{false};

  /// A trace that contradicts no member of a multipath group is not divergence.
  /// This switch exists only so the invariant can be proven by flipping it; it
  /// defaults to false and is recorded in the result.
  bool multipath_ambiguity_is_divergence{false};

  /// When true a comparison cannot report Match without a topology generation
  /// to correlate link identity against.
  bool require_topology_for_match{true};

  Result<void> validate() const;
};

/// The authority state the comparison is judged against. It is a snapshot: the
/// engine never looks anything up behind the caller's back.
struct AuthorityState {
  EpochId epoch{};
  IncarnationId incarnation{};
  SourceId route_source{};
  RevisionId route_revision{};
  SourceId topology_source{};
  RevisionId topology_revision{};
};

struct ComparisonContext {
  const RouteGeneration* routes{nullptr};
  const TopologyGeneration* topology{nullptr};
  const TopologyIndex* topology_index{nullptr};
  const SourceRegistry* sources{nullptr};
  AuthorityState authority{};
  TimePoint now{};
};

struct ComparisonRequest {
  RouteGenerationId generation{};
  MaybeId<PathGroupId> group{};
  MaybeId<PathId> path{};
  std::vector<ObservedPathEvidence> evidence{};
  /// Set by the runtime when the sequence ledger for one of the contributing
  /// sources holds an unrepaired gap. A gap is evidence of missing evidence and
  /// therefore downgrades completeness.
  bool source_sequence_gap{false};
};

struct EvidenceJudgement {
  EvidenceId id{};
  bool accepted{false};
  DivergenceClass rejection{DivergenceClass::None};
  Freshness freshness{Freshness::Unknown};
  ProofSurface surface{ProofSurface::Unknown};
  AuthorityClass authority{AuthorityClass::None};
  EvidenceCompleteness completeness{EvidenceCompleteness::Unknown};
  std::vector<DivergenceClass> classes;
  std::string reason;
};

struct HopDifference {
  std::uint32_t index{0};
  DivergenceClass detail{DivergenceClass::None};
  ExpectedHop expected{};
  ObservedHop observed{};
  bool has_expected{false};
  bool has_observed{false};
};

struct ComparisonResult {
  ComparisonId id{};
  RouteGenerationId generation{};
  TopologyGenerationId topology{};
  MaybeId<PathGroupId> group{};
  MaybeId<PathId> path{};
  MaybeId<PathId> matched_path{};
  MatchOutcome outcome{MatchOutcome::NoExpectedState};
  DivergenceClass primary{DivergenceClass::None};
  std::vector<DivergenceClass> classes{};
  Freshness freshness{Freshness::Unknown};
  EvidenceCompleteness completeness{EvidenceCompleteness::Unknown};
  ObservationConsistency consistency{ObservationConsistency::Unknown};
  ProofSurface surface{ProofSurface::Unknown};
  bool multipath{false};
  bool multipath_ambiguous{false};
  std::size_t evidence_offered{0};
  std::size_t evidence_considered{0};
  std::size_t evidence_rejected{0};
  std::size_t duplicate_evidence{0};
  std::vector<EvidenceJudgement> judgements{};
  std::vector<HopDifference> differences{};
  std::vector<std::string> rationale{};
  TimePoint evaluated_at{};

  /// Canonical encoding of everything that defines the classification. Free
  /// text rationale is excluded on purpose: explanations may be improved
  /// without changing identity, but any change to a class, a generation, an
  /// evidence set or a freshness verdict changes it.
  void write_canonical(CanonicalWriter& writer) const;
  Digest canonical_digest() const;
  void seal();

  bool has_class(DivergenceClass value) const;
  /// True only when the result is a compliance statement: outcome Match.
  bool is_compliance() const { return pathobs::is_compliance(outcome); }
};

class PATHOBS_API ComparisonEngine {
 public:
  ComparisonEngine(Limits limits, ComparisonPolicy policy);

  Result<ComparisonResult> compare(const ComparisonRequest& request,
                                   const ComparisonContext& context) const;

  const Limits& limits() const noexcept { return limits_; }
  const ComparisonPolicy& policy() const noexcept { return policy_; }

 private:
  Limits limits_;
  ComparisonPolicy policy_;
};

} // namespace pathobs

#endif // PATHOBS_COMPARE_HPP
