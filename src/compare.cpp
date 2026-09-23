// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/compare.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace pathobs {
namespace {

/// Evidence plus the working state the engine derives from it. Correlation may
/// add link identities to the working hop trace, but never to the record the
/// caller supplied: an evidence identity is a function of what the source said,
/// not of what the observatory later resolved.
struct Working {
  ObservedPathEvidence evidence;
  std::vector<ObservedHop> hops;
  Freshness freshness{Freshness::Unknown};
  std::string freshness_reason;
  EvidenceCompleteness completeness{EvidenceCompleteness::Unknown};
  std::vector<DivergenceClass> classes;
  DivergenceClass rejection{DivergenceClass::None};
  bool accepted{false};
  std::string reason;
  Digest trace_digest{};
  bool matches_a_member{false};
  bool trace_is_prefix{false};
  bool trace_is_exact{false};
  std::vector<PathId> matched_members;
};

bool traces_compatible(const std::vector<ObservedHop>& a, const std::vector<ObservedHop>& b) {
  const std::size_t common = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < common; ++i) {
    if (a[i].device != b[i].device) {
      return false;
    }
    if (a[i].ingress.valid() && b[i].ingress.valid() && a[i].ingress != b[i].ingress) {
      return false;
    }
    if (a[i].egress.valid() && b[i].egress.valid() && a[i].egress != b[i].egress) {
      return false;
    }
    if (a[i].link.valid() && b[i].link.valid() &&
        a[i].link_provenance != HopLinkProvenance::Absent &&
        b[i].link_provenance != HopLinkProvenance::Absent && a[i].link != b[i].link) {
      return false;
    }
  }
  return true;
}

Digest trace_digest(const std::vector<ObservedHop>& hops) {
  CanonicalWriter writer;
  writer.text("pathobs.trace");
  writer.count(hops.size());
  for (const auto& hop : hops) {
    writer.u32(hop.index);
    writer.id(hop.device);
    writer.id(hop.ingress);
    writer.id(hop.egress);
    writer.id(hop.link);
    writer.u8(static_cast<std::uint8_t>(hop.link_provenance));
  }
  return writer.digest();
}

DivergenceClass per_hop_class(const ExpectedHop& expected, const ObservedHop& observed) {
  if (expected.device != observed.device) {
    return DivergenceClass::DeviceMismatch;
  }
  if (observed.ingress.valid() && expected.ingress != observed.ingress) {
    return DivergenceClass::PortMismatch;
  }
  if (observed.egress.valid() && expected.egress != observed.egress) {
    return DivergenceClass::PortMismatch;
  }
  if (expected.has_egress_link && observed.link.valid() &&
      observed.link_provenance != HopLinkProvenance::Absent && expected.egress_link != observed.link) {
    return DivergenceClass::LinkMismatch;
  }
  return DivergenceClass::None;
}

void add_class(std::vector<DivergenceClass>& classes, DivergenceClass value) {
  if (value == DivergenceClass::None) {
    return;
  }
  if (std::find(classes.begin(), classes.end(), value) == classes.end()) {
    classes.push_back(value);
  }
}

/// True when both device multisets are equal but the device sequence differs.
bool same_devices_different_order(const std::vector<ExpectedHop>& expected,
                                  const std::vector<ObservedHop>& observed) {
  if (expected.size() != observed.size() || expected.empty()) {
    return false;
  }
  std::vector<DeviceId> left;
  std::vector<DeviceId> right;
  left.reserve(expected.size());
  right.reserve(observed.size());
  for (const auto& hop : expected) {
    left.push_back(hop.device);
  }
  for (const auto& hop : observed) {
    right.push_back(hop.device);
  }
  std::sort(left.begin(), left.end());
  std::sort(right.begin(), right.end());
  if (left != right) {
    return false;
  }
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (expected[i].device != observed[i].device) {
      return true;
    }
  }
  return false;
}

} // namespace

Result<void> ComparisonPolicy::validate() const {
  const Status freshness_valid = freshness.validate();
  if (!freshness_valid.has_value()) {
    return freshness_valid.error();
  }
  return ok();
}

void ComparisonResult::write_canonical(CanonicalWriter& writer) const {
  writer.text("pathobs.comparison");
  writer.id(generation);
  writer.id(topology);
  writer.boolean(group.present());
  writer.id(group.value());
  writer.boolean(path.present());
  writer.id(path.value());
  writer.boolean(matched_path.present());
  writer.id(matched_path.value());
  writer.u8(static_cast<std::uint8_t>(outcome));
  writer.u8(static_cast<std::uint8_t>(primary));
  writer.count(classes.size());
  for (const auto value : classes) {
    writer.u8(static_cast<std::uint8_t>(value));
  }
  writer.u8(static_cast<std::uint8_t>(freshness));
  writer.u8(static_cast<std::uint8_t>(completeness));
  writer.u8(static_cast<std::uint8_t>(consistency));
  writer.u8(static_cast<std::uint8_t>(surface));
  writer.boolean(multipath);
  writer.boolean(multipath_ambiguous);
  // The counts that describe the submission rather than the judged set are
  // deliberately excluded: delivering the same record twice must not create a
  // different comparison identity.
  writer.u32(static_cast<std::uint32_t>(evidence_considered));
  writer.u32(static_cast<std::uint32_t>(evidence_rejected));
  writer.count(judgements.size());
  for (const auto& judgement : judgements) {
    writer.digest_id(judgement.id);
    writer.boolean(judgement.accepted);
    writer.u8(static_cast<std::uint8_t>(judgement.rejection));
    writer.u8(static_cast<std::uint8_t>(judgement.freshness));
    writer.u8(static_cast<std::uint8_t>(judgement.surface));
    writer.u8(static_cast<std::uint8_t>(judgement.authority));
    writer.u8(static_cast<std::uint8_t>(judgement.completeness));
    writer.count(judgement.classes.size());
    for (const auto value : judgement.classes) {
      writer.u8(static_cast<std::uint8_t>(value));
    }
  }
  writer.count(differences.size());
  for (const auto& difference : differences) {
    writer.u32(difference.index);
    writer.u8(static_cast<std::uint8_t>(difference.detail));
    writer.boolean(difference.has_expected);
    writer.boolean(difference.has_observed);
  }
  writer.i64(evaluated_at.unix_nanos());
}

Digest ComparisonResult::canonical_digest() const {
  CanonicalWriter writer;
  write_canonical(writer);
  return writer.digest();
}

void ComparisonResult::seal() { id = ComparisonId::from_digest(canonical_digest()); }

bool ComparisonResult::has_class(DivergenceClass value) const {
  return std::find(classes.begin(), classes.end(), value) != classes.end();
}

ComparisonEngine::ComparisonEngine(Limits limits, ComparisonPolicy policy)
    : limits_(limits), policy_(policy) {}

Result<ComparisonResult> ComparisonEngine::compare(const ComparisonRequest& request,
                                                   const ComparisonContext& context) const {
  const Status policy_valid = policy_.validate();
  if (!policy_valid.has_value()) {
    return policy_valid.error();
  }
  if (!request.generation.valid()) {
    return Error{ErrorCode::InvalidIdentity, "comparison request has no route generation"};
  }
  if (request.evidence.size() > limits_.max_evidence_per_comparison) {
    return Error{ErrorCode::CapacityExceeded,
                 "comparison request exceeds the configured evidence bound"};
  }

  ComparisonResult result;
  result.generation = request.generation;
  result.group = request.group;
  result.path = request.path;
  result.evaluated_at = context.now;
  result.evidence_offered = request.evidence.size();

  const RouteGeneration* routes = context.routes;
  const bool generation_matches =
      routes != nullptr && routes->id == request.generation &&
      routes->epoch == context.authority.epoch &&
      routes->incarnation == context.authority.incarnation;

  if (!generation_matches) {
    add_class(result.classes, DivergenceClass::RouteGenerationMismatch);
    result.primary = DivergenceClass::RouteGenerationMismatch;
    result.outcome = MatchOutcome::Indeterminate;
    result.rationale.push_back(
        "route generation mismatch: the request names generation " + request.generation.str() +
        " but the authority state does not contain it at epoch " +
        context.authority.epoch.str() + " incarnation " + context.authority.incarnation.str());
    result.seal();
    return result;
  }

  result.topology = routes->topology;
  bool topology_matches = true;
  if (context.topology == nullptr) {
    topology_matches = false;
  } else if (context.topology->id != routes->topology ||
             context.topology->epoch != context.authority.epoch ||
             context.topology->incarnation != context.authority.incarnation) {
    topology_matches = false;
  }
  if (!topology_matches) {
    add_class(result.classes, DivergenceClass::TopologyGenerationMismatch);
  }

  // Resolve the subject of the comparison.
  //
  // An explicit subject wins. When the caller names a group or a path, and the
  // plan does not contain that identity, the answer is "no expected state":
  // silently substituting the subject an evidence record happens to mention
  // would compare something the caller never asked about.
  const PathGroup* group = nullptr;
  bool subject_explicit = false;
  if (request.group.present()) {
    subject_explicit = true;
    group = routes->find_group(request.group.value());
  } else if (request.path.present()) {
    subject_explicit = true;
    const ExpectedPath* path = routes->find_path(request.path.value());
    if (path != nullptr) {
      group = routes->find_group(path->group);
    }
  }
  if (group == nullptr && !subject_explicit) {
    for (const auto& evidence : request.evidence) {
      if (evidence.group.present()) {
        group = routes->find_group(evidence.group.value());
      } else if (evidence.path.present()) {
        const ExpectedPath* path = routes->find_path(evidence.path.value());
        if (path != nullptr) {
          group = routes->find_group(path->group);
        }
      } else if (evidence.origin.present() && evidence.destination.present()) {
        bool ambiguous = false;
        group = routes->find_group_by_endpoints(evidence.origin.value(),
                                                evidence.destination.value(), &ambiguous);
      }
      if (group != nullptr) {
        break;
      }
    }
  }

  if (group == nullptr) {
    result.outcome = MatchOutcome::NoExpectedState;
    result.primary = DivergenceClass::None;
    result.rationale.push_back(subject_explicit
                                   ? "the named subject is not present in the current plan"
                                   : "no expected state could be associated with the evidence");
    result.rationale.push_back(
        "no expected path state for the subject of this comparison in generation " +
        request.generation.str());
    result.seal();
    return result;
  }
  result.group = MaybeId<PathGroupId>(group->id);
  result.multipath = group->multipath();

  // ---------------------------------------------------------------------
  // Judge every evidence record. Fencing happens before anything else: fenced
  // evidence is excluded from the classification entirely, which is how a
  // replayed or stale record is prevented from validating current behaviour.
  // ---------------------------------------------------------------------
  std::vector<Working> working;
  working.reserve(request.evidence.size());
  for (const auto& evidence : request.evidence) {
    Working item;
    item.evidence = evidence;

    const Status shape = evidence.validate(limits_);
    if (!shape.has_value()) {
      item.rejection = DivergenceClass::EvidenceUnknown;
      item.reason = "evidence record failed validation: " + shape.error().message;
      add_class(item.classes, DivergenceClass::EvidenceUnknown);
      working.push_back(std::move(item));
      continue;
    }

    FreshnessPolicy effective = policy_.freshness;
    if (context.sources != nullptr) {
      const Result<SourceDescriptor> descriptor = context.sources->find(evidence.source);
      if (!descriptor.has_value()) {
        item.rejection = DivergenceClass::AuthorityInsufficient;
        item.reason = "evidence was produced by an unregistered source";
        add_class(item.classes, DivergenceClass::AuthorityInsufficient);
        working.push_back(std::move(item));
        continue;
      }
      const SourceDescriptor& source = descriptor.value();
      if (source.max_observation_age.count() > 0) {
        effective.evidence_max_age = source.max_observation_age;
        effective.evidence_expiry = std::max(source.max_observation_age,
                                             policy_.freshness.evidence_expiry);
      }
      if (source.clock_skew_tolerance.count() > 0) {
        effective.clock_skew_tolerance = source.clock_skew_tolerance;
      }
      if (evidence.authority > source.authority) {
        item.rejection = DivergenceClass::AuthorityInsufficient;
        item.reason = "evidence claims more authority than its source is registered with";
        add_class(item.classes, DivergenceClass::AuthorityInsufficient);
        working.push_back(std::move(item));
        continue;
      }
    }

    const FreshnessAssessment freshness = assess_evidence(evidence, context.now, effective);
    item.freshness = freshness.state;
    item.freshness_reason = freshness.reason;

    if (evidence.epoch != context.authority.epoch) {
      add_class(item.classes, evidence.epoch < context.authority.epoch
                                  ? DivergenceClass::EpochFenced
                                  : DivergenceClass::EpochAhead);
    }
    if (evidence.claimed_incarnation != context.authority.incarnation) {
      add_class(item.classes, DivergenceClass::IncarnationFenced);
    }
    if (evidence.route_generation.present() &&
        evidence.route_generation.value() != request.generation) {
      add_class(item.classes, DivergenceClass::RouteGenerationMismatch);
    }
    if (evidence.topology_generation.present() && evidence.topology_generation.value() != routes->topology) {
      add_class(item.classes, DivergenceClass::TopologyGenerationMismatch);
    }
    if (evidence.revision < context.authority.route_revision) {
      add_class(item.classes, DivergenceClass::RevisionFenced);
    } else if (evidence.revision > context.authority.route_revision) {
      add_class(item.classes, DivergenceClass::RevisionAhead);
    }
    if (evidence.surface == ProofSurface::Unsupported) {
      add_class(item.classes, DivergenceClass::EvidenceUnsupported);
    } else if (evidence.surface == ProofSurface::Unknown) {
      add_class(item.classes, DivergenceClass::EvidenceUnknown);
    }
    if (policy_.require_advisory_or_higher && evidence.authority < AuthorityClass::Advisory) {
      add_class(item.classes, DivergenceClass::AuthorityInsufficient);
    }

    const DivergenceClass rejection = select_primary(item.classes.data(), item.classes.size());
    if (rejection != DivergenceClass::None) {
      item.rejection = rejection;
      item.reason = std::string("excluded as ") + to_code(rejection);
      working.push_back(std::move(item));
      continue;
    }

    item.accepted = true;
    item.hops = evidence.hops;

    // Link correlation. Only records that state no link are correlated, and the
    // correlation never rewrites the evidence identity.
    if (context.topology_index != nullptr && context.topology_index->valid()) {
      for (auto& hop : item.hops) {
        if (hop.link.valid() || !hop.device.valid() || !hop.egress.valid()) {
          continue;
        }
        const Result<LinkResolution> resolution =
            context.topology_index->resolve_link(hop.device, hop.egress);
        if (resolution.has_value() && resolution.value().known) {
          hop.link = resolution.value().link;
          hop.link_provenance = HopLinkProvenance::Correlated;
        }
      }
    }

    item.trace_digest = trace_digest(item.hops);
    item.reason = freshness.reason;
    working.push_back(std::move(item));
  }

  // Deterministic order: authority, freshness, completeness, receive time,
  // sequence, identity. Every later step reads this order and nothing else, so
  // permuting the caller's evidence cannot change the outcome.
  std::sort(working.begin(), working.end(), [](const Working& a, const Working& b) {
    if (a.evidence.authority != b.evidence.authority) {
      return a.evidence.authority > b.evidence.authority;
    }
    if (freshness_severity(a.freshness) != freshness_severity(b.freshness)) {
      return freshness_severity(a.freshness) < freshness_severity(b.freshness);
    }
    if (a.evidence.declared_completeness != b.evidence.declared_completeness) {
      return a.evidence.declared_completeness > b.evidence.declared_completeness;
    }
    if (!(a.evidence.received_at == b.evidence.received_at)) {
      return a.evidence.received_at > b.evidence.received_at;
    }
    if (!(a.evidence.sequence == b.evidence.sequence)) {
      return a.evidence.sequence > b.evidence.sequence;
    }
    return a.evidence.id < b.evidence.id;
  });

  // Exact duplicates are one record. Counting them twice would let a source
  // manufacture agreement by repetition.
  {
    std::vector<Working> unique;
    unique.reserve(working.size());
    for (auto& item : working) {
      const bool duplicate =
          !unique.empty() && unique.back().evidence.id == item.evidence.id;
      if (duplicate) {
        ++result.duplicate_evidence;
        continue;
      }
      unique.push_back(std::move(item));
    }
    working = std::move(unique);
  }

  std::vector<const Working*> accepted;
  for (const auto& item : working) {
    if (item.accepted) {
      accepted.push_back(&item);
    } else {
      ++result.evidence_rejected;
    }
  }
  result.evidence_considered = accepted.size();

  result.judgements.reserve(working.size());
  for (const auto& item : working) {
    EvidenceJudgement judgement;
    judgement.id = item.evidence.id;
    judgement.accepted = item.accepted;
    judgement.rejection = item.rejection;
    judgement.freshness = item.freshness;
    judgement.surface = item.evidence.surface;
    judgement.authority = item.evidence.authority;
    judgement.completeness = item.evidence.claimed_completeness();
    judgement.classes = item.classes;
    judgement.reason = item.reason;
    result.judgements.push_back(std::move(judgement));
  }

  // Every exclusion is part of the classification, not a private note on one
  // evidence record: a caller reading the class set must see why records were
  // left out of it.
  for (const auto& item : working) {
    if (!item.accepted && item.rejection != DivergenceClass::None) {
      add_class(result.classes, item.rejection);
    }
  }

  if (request.source_sequence_gap) {
    add_class(result.classes, DivergenceClass::ObservationGap);
    result.rationale.push_back(
        "the source sequence ledger holds an unrepaired gap for at least one contributing source");
  }

  if (accepted.empty()) {
    DivergenceClass rejection = DivergenceClass::EvidenceUnknown;
    for (const auto& item : working) {
      if (item.rejection == DivergenceClass::None) {
        continue;
      }
      if (rejection == DivergenceClass::EvidenceUnknown ||
          precedence_of(item.rejection) < precedence_of(rejection)) {
        rejection = item.rejection;
      }
    }
    add_class(result.classes, rejection);
    result.primary = select_primary(result.classes.data(), result.classes.size());
    // A record that was excluded because its proof surface cannot be judged
    // yields "unsupported", which says more than "indeterminate": the runtime
    // is not able to answer, rather than not having enough to answer with.
    result.outcome = rejection == DivergenceClass::EvidenceUnsupported
                         ? MatchOutcome::Unsupported
                         : MatchOutcome::Indeterminate;
    result.freshness = Freshness::Unknown;
    result.completeness = EvidenceCompleteness::Unknown;
    result.consistency = rejection == DivergenceClass::EvidenceUnsupported
                             ? ObservationConsistency::Unsupported
                             : ObservationConsistency::Unknown;
    for (const auto& entry : working) {
      if (!entry.accepted && entry.rejection == DivergenceClass::EvidenceUnsupported) {
        result.surface = ProofSurface::Unsupported;
      }
    }
    result.rationale.push_back(
        "absence of evidence is not evidence: no observation record survived the fencing and "
        "quality gates, so the comparison is indeterminate");
    result.seal();
    return result;
  }

  // ---------------------------------------------------------------------
  // Match the traces against the members of the group.
  // ---------------------------------------------------------------------
  const Working& representative = *accepted.front();
  std::vector<const ExpectedPath*> members;
  members.reserve(group->members.size());
  for (const auto member : group->members) {
    const ExpectedPath* path = routes->find_path(member);
    if (path != nullptr) {
      members.push_back(path);
    }
  }

  bool any_matches_member = false;
  for (auto& item : working) {
    if (!item.accepted) {
      continue;
    }
    for (const ExpectedPath* member : members) {
      const TraceComparison comparison = compare_trace(*member, item.hops);
      if (comparison.match == TraceMatch::Exact) {
        item.trace_is_exact = true;
        item.matches_a_member = true;
        if (std::find(item.matched_members.begin(), item.matched_members.end(), member->id) ==
            item.matched_members.end()) {
          item.matched_members.push_back(member->id);
        }
      } else if (comparison.match == TraceMatch::Prefix) {
        item.trace_is_prefix = true;
        item.matches_a_member = true;
        if (std::find(item.matched_members.begin(), item.matched_members.end(), member->id) ==
            item.matched_members.end()) {
          item.matched_members.push_back(member->id);
        }
      }
    }
    any_matches_member = any_matches_member || item.matches_a_member;
  }

  // The reference path is a deterministic choice: the claimed path when it is a
  // member of the group, otherwise the member with the smallest identity.
  const ExpectedPath* reference = nullptr;
  if (request.path.present()) {
    const ExpectedPath* claimed = routes->find_path(request.path.value());
    if (claimed != nullptr && claimed->group == group->id) {
      reference = claimed;
    }
  }
  if (reference == nullptr) {
    for (const auto& evidence : request.evidence) {
      if (!evidence.path.present()) {
        continue;
      }
      const ExpectedPath* claimed = routes->find_path(evidence.path.value());
      if (claimed != nullptr && claimed->group == group->id) {
        reference = claimed;
        break;
      }
    }
  }
  if (reference == nullptr && !members.empty()) {
    reference = *std::min_element(members.begin(), members.end(),
                                  [](const ExpectedPath* a, const ExpectedPath* b) {
                                    return a->id < b->id;
                                  });
  }

  result.matched_path =
      representative.matched_members.empty()
          ? MaybeId<PathId>{}
          : MaybeId<PathId>(representative.matched_members.front());

  const std::size_t distinct_members = [&]() {
    std::set<PathId> distinct;
    for (const auto& item : working) {
      for (const auto member : item.matched_members) {
        distinct.insert(member);
      }
    }
    return distinct.size();
  }();

  // ---------------------------------------------------------------------
  // Consistency.
  // ---------------------------------------------------------------------
  bool conflicting = false;
  for (std::size_t i = 0; i < accepted.size() && !conflicting; ++i) {
    for (std::size_t j = i + 1; j < accepted.size(); ++j) {
      const Working& left = *accepted[i];
      const Working& right = *accepted[j];
      const bool same_claimed_path =
          left.evidence.path.present() && right.evidence.path.present() &&
          left.evidence.path.value() == right.evidence.path.value();
      if (!same_claimed_path) {
        continue;
      }
      if (!traces_compatible(left.hops, right.hops)) {
        conflicting = true;
        result.rationale.push_back(
            "two accepted records claim path " + left.evidence.path.value().str() +
            " and disagree about the hop trace; the plan declares one trace for that path");
        break;
      }
    }
  }

  ProofSurface worst_surface = ProofSurface::Real;
  for (const auto* item : accepted) {
    if (static_cast<std::uint8_t>(item->evidence.surface) >
        static_cast<std::uint8_t>(worst_surface)) {
      worst_surface = item->evidence.surface;
    }
  }
  result.surface = worst_surface;

  Freshness worst_freshness = Freshness::Fresh;
  for (const auto* item : accepted) {
    worst_freshness = worse_of(worst_freshness, item->freshness);
  }
  result.freshness = worst_freshness;

  if (conflicting) {
    result.consistency = ObservationConsistency::Conflicting;
    add_class(result.classes, DivergenceClass::EvidenceConflicting);
  } else if (worst_surface == ProofSurface::Unsupported) {
    result.consistency = ObservationConsistency::Unsupported;
  } else if (distinct_members > 1) {
    result.consistency = ObservationConsistency::Ambiguous;
    result.multipath_ambiguous = true;
    add_class(result.classes, DivergenceClass::EvidenceAmbiguous);
    result.rationale.push_back("the accepted records are consistent with " +
                               std::to_string(distinct_members) +
                               " members of the group: legitimate multipath, not divergence");
  } else {
    result.consistency = ObservationConsistency::Consistent;
  }

  // ---------------------------------------------------------------------
  // Completeness, derived rather than trusted.
  // ---------------------------------------------------------------------
  EvidenceCompleteness completeness = EvidenceCompleteness::Complete;
  for (const auto* item : accepted) {
    if (item->evidence.source_truncated) {
      completeness = EvidenceCompleteness::Partial;
    }
    if (item->evidence.declared_completeness == EvidenceCompleteness::Partial) {
      completeness = EvidenceCompleteness::Partial;
    }
    if (!hop_indices_are_contiguous(item->hops)) {
      completeness = EvidenceCompleteness::Partial;
    }
    if (item->trace_is_prefix && !item->trace_is_exact) {
      completeness = EvidenceCompleteness::Partial;
    }
    if (!item->matches_a_member && !members.empty()) {
      // A trace that contradicts the plan inside the range it observed is a
      // definitive statement about that range, so it does not by itself make
      // the evidence incomplete. A trace that is merely shorter than every
      // member is incomplete.
      std::size_t shortest = members.front()->hops.size();
      for (const ExpectedPath* member : members) {
        shortest = std::min(shortest, member->hops.size());
      }
      if (item->hops.size() < shortest) {
        completeness = EvidenceCompleteness::Partial;
      }
    }
  }
  if (request.source_sequence_gap) {
    // A gap in a source's sequence means records are missing from the world,
    // not merely from this request. The observation cannot be complete.
    completeness = EvidenceCompleteness::Partial;
  }
  result.completeness = completeness;

  // ---------------------------------------------------------------------
  // Hop level differences, computed against the reference path. They are
  // reported only when the trace contradicts every member of the group, which
  // is what keeps a legitimate alternate member from looking like a mismatch.
  // ---------------------------------------------------------------------
  // Contradiction requires something to have been observed. A record with no
  // hop trace at all contradicts nothing; it is incomplete, not divergent.
  const bool structural = !any_matches_member && !representative.hops.empty();
  if (structural && reference != nullptr) {
    const std::size_t span = std::max(reference->hops.size(), representative.hops.size());
    bool order_violation = same_devices_different_order(reference->hops, representative.hops);
    for (std::size_t i = 0; i < span; ++i) {
      HopDifference difference;
      difference.index = static_cast<std::uint32_t>(i);
      const bool has_expected = i < reference->hops.size();
      const bool has_observed = i < representative.hops.size();
      difference.has_expected = has_expected;
      difference.has_observed = has_observed;
      if (has_expected) {
        difference.expected = reference->hops[i];
      }
      if (has_observed) {
        difference.observed = representative.hops[i];
      }
      if (has_expected && has_observed) {
        DivergenceClass detail = per_hop_class(reference->hops[i], representative.hops[i]);
        if (order_violation && detail == DivergenceClass::DeviceMismatch) {
          detail = DivergenceClass::HopOrderViolation;
        }
        difference.detail = detail;
      } else if (!has_expected && has_observed) {
        difference.detail = DivergenceClass::HopUnexpected;
      } else {
        difference.detail = DivergenceClass::HopMissing;
      }
      if (difference.detail == DivergenceClass::None) {
        continue;
      }
      add_class(result.classes, difference.detail);
      result.differences.push_back(difference);
    }
  }

  // Link identity that the plan declares but the evidence cannot support. A hop
  // that already diverged is excluded: its missing link is a consequence of the
  // divergence, not a second independent problem.
  if (reference != nullptr) {
    const std::size_t span = std::min(reference->hops.size(), representative.hops.size());
    for (std::size_t i = 0; i < span; ++i) {
      const bool already_diverged =
          std::any_of(result.differences.begin(), result.differences.end(),
                      [i](const HopDifference& difference) { return difference.index == i; });
      if (already_diverged) {
        continue;
      }
      if (!reference->hops[i].has_egress_link) {
        continue;
      }
      if (!representative.hops[i].link.valid() ||
          representative.hops[i].link_provenance == HopLinkProvenance::Absent) {
        add_class(result.classes, DivergenceClass::LinkUnresolved);
        completeness = EvidenceCompleteness::Partial;
        result.completeness = completeness;
        break;
      }
    }
  }

  // PathUnknownToPlan means the identity an observation names is not in the plan
  // at all. A trace that diverges from a path the plan does contain is a
  // structural divergence, not an unknown path.
  {
    bool claimed_anything = false;
    bool claimed_identity_known = false;
    for (const auto* item : accepted) {
      if (item->evidence.path.present()) {
        claimed_anything = true;
        if (routes->find_path(item->evidence.path.value()) != nullptr) {
          claimed_identity_known = true;
        }
      } else if (item->evidence.group.present()) {
        claimed_anything = true;
        if (routes->find_group(item->evidence.group.value()) != nullptr) {
          claimed_identity_known = true;
        }
      }
    }
    if (request.path.present() && routes->find_path(request.path.value()) == nullptr) {
      add_class(result.classes, DivergenceClass::PathUnknownToPlan);
    } else if (claimed_anything && !claimed_identity_known) {
      add_class(result.classes, DivergenceClass::PathUnknownToPlan);
    }
  }

  // ---------------------------------------------------------------------
  // Outcome. The order of these gates is the contract:
  //   surface, conflict, freshness, divergence, completeness, authority, match.
  // Only the last gate produces compliance, and every earlier gate that trips
  // makes the comparison indeterminate or divergent. Incomplete or stale
  // evidence therefore can never become compliance.
  // ---------------------------------------------------------------------

  MatchOutcome outcome = MatchOutcome::Match;
  if (worst_surface == ProofSurface::Unsupported) {
    outcome = MatchOutcome::Unsupported;
    add_class(result.classes, DivergenceClass::EvidenceUnsupported);
    result.rationale.push_back(
        "the accepted evidence belongs to a proof surface this build cannot judge");
  } else if (worst_surface == ProofSurface::Unknown) {
    outcome = MatchOutcome::Indeterminate;
    add_class(result.classes, DivergenceClass::EvidenceUnknown);
    result.rationale.push_back("the accepted evidence does not state a proof surface");
  }

  if (outcome != MatchOutcome::Unsupported && conflicting) {
    outcome = MatchOutcome::Indeterminate;
  }

  if (outcome != MatchOutcome::Unsupported && !conflicting &&
      !is_compliance_capable(worst_freshness)) {
    outcome = MatchOutcome::Indeterminate;
    switch (worst_freshness) {
      case Freshness::Stale:
        add_class(result.classes, DivergenceClass::EvidenceStale);
        break;
      case Freshness::Expired:
        add_class(result.classes, DivergenceClass::EvidenceExpired);
        break;
      default:
        add_class(result.classes, DivergenceClass::EvidenceUnknown);
        break;
    }
    result.rationale.push_back(
        std::string("evidence freshness is ") + to_string(worst_freshness) +
        ": stale or unjudgeable evidence cannot validate current behaviour");
  }

  if (outcome == MatchOutcome::Match && structural) {
    outcome = MatchOutcome::Divergent;
  }

  if (outcome == MatchOutcome::Match && policy_.multipath_ambiguity_is_divergence &&
      result.multipath_ambiguous) {
    // The strict reading a caller may ask for: an observation that cannot say
    // which member of a multipath group it followed is not reported as
    // compliance. The default policy keeps the lenient reading, and this switch
    // exists so the invariant can be shown to be a policy choice.
    outcome = MatchOutcome::Indeterminate;
    add_class(result.classes, DivergenceClass::EvidenceAmbiguous);
    result.rationale.push_back(
        "policy treats multipath ambiguity as a failure to determine the path");
  }

  if (outcome == MatchOutcome::Match && completeness != EvidenceCompleteness::Complete) {
    outcome = MatchOutcome::Indeterminate;
    add_class(result.classes, DivergenceClass::EvidenceIncomplete);
    result.rationale.push_back(
        "the observation is incomplete: an incomplete observation is never compliance");
  }

  if (outcome == MatchOutcome::Match) {
    if (policy_.require_topology_for_match && !topology_matches) {
      outcome = MatchOutcome::Indeterminate;
      add_class(result.classes, DivergenceClass::TopologyGenerationMismatch);
      result.rationale.push_back(
          "no topology generation is available to correlate link identity against");
    } else if (representative.evidence.authority < AuthorityClass::Advisory) {
      outcome = MatchOutcome::Indeterminate;
      add_class(result.classes, DivergenceClass::AuthorityInsufficient);
      result.rationale.push_back(
          "the accepted evidence does not carry enough authority to support compliance");
    } else if (policy_.require_real_surface_for_match && worst_surface != ProofSurface::Real) {
      outcome = MatchOutcome::Indeterminate;
      add_class(result.classes, DivergenceClass::EvidenceUnsupported);
      result.rationale.push_back("policy requires real evidence for a compliance statement");
    }
  }

  if (outcome == MatchOutcome::Match && group->members.size() > 0 &&
      representative.matched_members.empty() && !any_matches_member) {
    // Unreachable by construction, but stated explicitly rather than assumed.
    outcome = MatchOutcome::Indeterminate;
    add_class(result.classes, DivergenceClass::EvidenceUnknown);
  }

  // A compliance statement must name the path it is about.
  if (outcome == MatchOutcome::Match && !result.matched_path.present() && !members.empty()) {
    outcome = MatchOutcome::Indeterminate;
    add_class(result.classes, DivergenceClass::EvidenceUnknown);
  }

  result.outcome = outcome;
  result.primary = select_primary(result.classes.data(), result.classes.size());

  result.rationale.push_back("outcome " + std::string(to_string(outcome)) + " with primary class " +
                             to_code(result.primary) + " from " +
                             std::to_string(result.evidence_considered) +
                             " accepted record(s) and " +
                             std::to_string(result.evidence_rejected) + " rejected record(s)");
  result.seal();
  return result;
}

} // namespace pathobs
