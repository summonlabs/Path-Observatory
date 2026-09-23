// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "test_framework.hpp"

#include "pathobs/compare.hpp"
#include "pathobs/divergence.hpp"
#include "pathobs/freshness.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/topology.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace pathobs;

namespace {

struct Harness {
  synthetic::LabFabric fabric;
  TopologyIndex index;
  ComparisonContext context;
};

/// The context holds pointers into the fabric, so it is bound once the harness
/// has come to rest at its final address. Binding inside the factory would
/// leave the pointers aimed at the local object the factory moved from.
void bind(Harness& harness) {
  harness.context.routes = &harness.fabric.routes;
  harness.context.topology = &harness.fabric.topology;
  harness.context.topology_index = &harness.index;
  harness.context.authority.epoch = harness.fabric.routes.epoch;
  harness.context.authority.incarnation = harness.fabric.routes.incarnation;
  harness.context.authority.route_revision = harness.fabric.routes.revision;
  harness.context.authority.topology_revision = harness.fabric.topology.revision;
  harness.context.now = harness.fabric.routes.received;
}

Result<Harness> make_harness(const synthetic::LabOptions& options) {
  Harness harness;
  PATHOBS_TRY(fabric, synthetic::build_lab_fabric(options, default_limits()));
  harness.fabric = std::move(fabric);
  PATHOBS_TRY(index, TopologyIndex::build(harness.fabric.topology, default_limits()));
  harness.index = std::move(index);
  return harness;
}

ComparisonRequest make_request(const Harness& harness) {
  ComparisonRequest request;
  request.generation = harness.fabric.routes.id;
  request.group = MaybeId<PathGroupId>(harness.fabric.group);
  request.evidence = harness.fabric.evidence;
  return request;
}

Result<ComparisonResult> evaluate(Harness& harness, const ComparisonRequest& request) {
  const ComparisonEngine engine(default_limits(), ComparisonPolicy{});
  return engine.compare(request, harness.context);
}

Result<ComparisonResult> run(Harness& harness, const ComparisonRequest& request) {
  bind(harness);
  return evaluate(harness, request);
}

} // namespace

PATHOBS_TEST(topology, finalize_rejects_inconsistent_generations) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());

  TopologyGeneration duplicate = harness.value().fabric.topology;
  duplicate.devices.push_back(duplicate.devices.front());
  CHECK(!duplicate.finalize(default_limits()).has_value());

  TopologyGeneration orphan_port = harness.value().fabric.topology;
  PortRecord orphan;
  orphan.id = PortId::from_value(900001);
  orphan.device = DeviceId::from_value(424242);
  orphan_port.ports.push_back(orphan);
  CHECK(!orphan_port.finalize(default_limits()).has_value());

  TopologyGeneration bad_link = harness.value().fabric.topology;
  bad_link.links.front().local_port = PortId::from_value(777777);
  CHECK(!bad_link.finalize(default_limits()).has_value());

  TopologyGeneration no_identity = harness.value().fabric.topology;
  no_identity.id = TopologyGenerationId{};
  CHECK(!no_identity.finalize(default_limits()).has_value());
}

PATHOBS_TEST(topology, link_correlation_is_conservative) {
  synthetic::LabOptions lab;
  lab.hop_count = 3;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());

  const TopologyGeneration& topology = harness.value().fabric.topology;
  const ExpectedPath& path = harness.value().fabric.routes.paths.front();

  const auto resolved =
      harness.value().index.resolve_link(path.hops[0].device, path.hops[0].egress);
  REQUIRE(resolved.has_value());
  CHECK(resolved.value().known);
  CHECK(!resolved.value().ambiguous);
  CHECK_EQ(resolved.value().link.value(), path.hops[0].egress_link.value());

  // The far end of the same link correlates as well.
  const auto reverse =
      harness.value().index.resolve_link(path.hops[1].device, path.hops[1].ingress);
  REQUIRE(reverse.has_value());
  CHECK(reverse.value().known);
  CHECK_EQ(reverse.value().link.value(), path.hops[0].egress_link.value());

  const auto unknown = harness.value().index.resolve_link(DeviceId::from_value(999999),
                                                          PortId::from_value(1));
  REQUIRE(unknown.has_value());
  CHECK(!unknown.value().known);
  CHECK_EQ(unknown.value().candidates, 0u);

  // Two links on the same port make the identity ambiguous rather than picking
  // one of them.
  TopologyGeneration ambiguous = topology;
  LinkRecord extra = ambiguous.links.front();
  extra.id = LinkId::from_value(extra.id.value() + 100000);
  ambiguous.links.push_back(extra);
  REQUIRE(ambiguous.finalize(default_limits()).has_value());
  const auto ambiguous_index = TopologyIndex::build(ambiguous, default_limits());
  REQUIRE(ambiguous_index.has_value());
  const auto ambiguous_result =
      ambiguous_index.value().resolve_link(path.hops[0].device, path.hops[0].egress);
  REQUIRE(ambiguous_result.has_value());
  CHECK(!ambiguous_result.value().known);
  CHECK(ambiguous_result.value().ambiguous);
  CHECK_EQ(ambiguous_result.value().candidates, 2u);
}

PATHOBS_TEST(freshness, classification_is_a_pure_function_of_the_evaluation_time) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const ObservedPathEvidence& evidence = harness.value().fabric.evidence.front();

  FreshnessPolicy policy;
  const TimePoint received = evidence.received_at;

  CHECK_EQ(assess_evidence(evidence, received, policy).state, Freshness::Fresh);
  CHECK_EQ(assess_evidence(
               evidence,
               TimePoint::from_unix_nanos(received.unix_nanos() + 10LL * 1000000000LL), policy).state,
           Freshness::Fresh);
  CHECK_EQ(
      assess_evidence(evidence,
                      TimePoint::from_unix_nanos(received.unix_nanos() + 60LL * 1000000000LL),
                      policy)
          .state,
      Freshness::Stale);
  CHECK_EQ(assess_evidence(
               evidence,
               TimePoint::from_unix_nanos(received.unix_nanos() + 3600LL * 1000000000LL), policy)
               .state,
           Freshness::Expired);

  // An evaluation time before the receive time beyond the skew tolerance cannot
  // be judged.
  CHECK_EQ(assess_evidence(
               evidence,
               TimePoint::from_unix_nanos(received.unix_nanos() - 60LL * 1000000000LL), policy)
               .state,
           Freshness::Future);
  CHECK_EQ(assess_evidence(evidence, TimePoint::unset(), policy).state, Freshness::Unknown);

  ObservedPathEvidence without_receive = evidence;
  without_receive.received_at = TimePoint::unset();
  CHECK_EQ(assess_evidence(without_receive, received, policy).state, Freshness::Unknown);

  // Severity ordering: fresh is the only value that can support compliance.
  CHECK(is_compliance_capable(Freshness::Fresh));
  CHECK(!is_compliance_capable(Freshness::Stale));
  CHECK(!is_compliance_capable(Freshness::Expired));
  CHECK(!is_compliance_capable(Freshness::Unknown));
  CHECK(!is_compliance_capable(Freshness::Future));
  CHECK_EQ(worse_of(Freshness::Fresh, Freshness::Stale), Freshness::Stale);
  CHECK_EQ(worse_of(Freshness::Stale, Freshness::Fresh), Freshness::Stale);
  CHECK_EQ(worse_of(Freshness::Unknown, Freshness::Fresh), Freshness::Unknown);

  FreshnessPolicy invalid;
  invalid.evidence_max_age = std::chrono::seconds(60);
  invalid.evidence_expiry = std::chrono::seconds(30);
  CHECK(!invalid.validate().has_value());
}

PATHOBS_TEST(divergence, the_class_list_is_closed_and_ordered) {
  std::size_t count = 0;
  const DivergenceDescriptor* descriptors = all_divergence_descriptors(&count);
  CHECK_EQ(count, 26u);

  std::set<std::string> codes;
  for (std::size_t i = 0; i < count; ++i) {
    const DivergenceDescriptor& descriptor = descriptors[i];
    CHECK_EQ(static_cast<std::size_t>(descriptor.value), i);
    CHECK(codes.insert(descriptor.code).second);
    CHECK_EQ(parse_divergence_code(descriptor.code).value(), descriptor.value);
    CHECK_EQ(static_cast<int>(describe(descriptor.value).value),
             static_cast<int>(descriptor.value));
  }
  CHECK(!parse_divergence_code("not_a_class").has_value());

  // Precedence is a total order and the selection does not depend on order.
  const DivergenceClass forward[] = {DivergenceClass::DeviceMismatch,
                                     DivergenceClass::EvidenceStale,
                                     DivergenceClass::EpochFenced};
  const DivergenceClass backward[] = {DivergenceClass::EpochFenced,
                                      DivergenceClass::EvidenceStale,
                                      DivergenceClass::DeviceMismatch};
  CHECK_EQ(select_primary(forward, 3), DivergenceClass::EpochFenced);
  CHECK_EQ(select_primary(backward, 3), DivergenceClass::EpochFenced);
  CHECK_EQ(select_primary(nullptr, 0), DivergenceClass::None);

  CHECK(indicates_divergence(DivergenceClass::DeviceMismatch));
  CHECK(!indicates_divergence(DivergenceClass::EvidenceStale));
  CHECK(is_fencing(DivergenceClass::EpochFenced));
  CHECK(is_evidence_quality(DivergenceClass::EvidenceIncomplete));
  CHECK(!is_compliance(MatchOutcome::Indeterminate));
  CHECK(is_compliance(MatchOutcome::Match));
}

PATHOBS_TEST(compare, a_complete_matching_observation_is_compliance) {
  synthetic::LabOptions lab;
  lab.omit_links = true;  // force correlation from the topology
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Match);
  CHECK(result.value().is_compliance());
  CHECK_EQ(result.value().primary, DivergenceClass::None);
  CHECK_EQ(result.value().freshness, Freshness::Fresh);
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Complete);
  CHECK_EQ(result.value().consistency, ObservationConsistency::Consistent);
  CHECK_EQ(result.value().surface, ProofSurface::Synthetic);
  CHECK(result.value().matched_path.present());
  CHECK_EQ(result.value().evidence_considered, 1u);
  CHECK(result.value().id.valid());
}

PATHOBS_TEST(compare, an_incomplete_observation_is_never_compliance) {
  synthetic::LabOptions lab;
  lab.partial = true;
  lab.hop_count = 4;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Partial);
  CHECK(result.value().has_class(DivergenceClass::EvidenceIncomplete));
}

PATHOBS_TEST(compare, an_empty_observation_is_never_compliance) {
  synthetic::LabOptions lab;
  lab.empty_trace = true;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Partial);
}

PATHOBS_TEST(compare, absent_evidence_is_not_evidence) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  request.evidence.clear();
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().evidence_considered, 0u);
  CHECK_EQ(result.value().freshness, Freshness::Unknown);
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Unknown);
}

PATHOBS_TEST(compare, stale_evidence_cannot_validate_current_behaviour) {
  synthetic::LabOptions lab;
  lab.evidence_age_nanos = -3600LL * 1000000000LL;  // one hour before the plan
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().freshness, Freshness::Expired);
  CHECK(result.value().has_class(DivergenceClass::EvidenceExpired));
}

PATHOBS_TEST(compare, fenced_evidence_is_excluded_and_reported) {
  synthetic::LabOptions lab;
  lab.stale_epoch = true;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK_EQ(result.value().evidence_considered, 0u);
  CHECK_EQ(result.value().evidence_rejected, 1u);
  CHECK_EQ(result.value().primary, DivergenceClass::EpochFenced);
  CHECK(result.value().has_class(DivergenceClass::EpochFenced));

  synthetic::LabOptions revision_lab;
  revision_lab.stale_revision = true;
  auto revision_harness = make_harness(revision_lab);
  REQUIRE(revision_harness.has_value());
  const auto revision_result =
      run(revision_harness.value(), make_request(revision_harness.value()));
  REQUIRE(revision_result.has_value());
  CHECK(revision_result.value().has_class(DivergenceClass::RevisionFenced));
  CHECK(!revision_result.value().is_compliance());

  synthetic::LabOptions unknown_lab;
  unknown_lab.unknown_generation = true;
  auto unknown_harness = make_harness(unknown_lab);
  REQUIRE(unknown_harness.has_value());
  const auto unknown_result = run(unknown_harness.value(), make_request(unknown_harness.value()));
  REQUIRE(unknown_result.has_value());
  CHECK(unknown_result.value().has_class(DivergenceClass::RouteGenerationMismatch));
  CHECK(!unknown_result.value().is_compliance());
}

PATHOBS_TEST(compare, a_divergent_trace_is_reported_with_hop_detail) {
  synthetic::LabOptions lab;
  lab.diverge = true;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Divergent);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().primary, DivergenceClass::DeviceMismatch);
  CHECK(result.value().has_class(DivergenceClass::DeviceMismatch));
  REQUIRE(result.value().differences.size() >= 1u);
  CHECK_EQ(result.value().differences.front().index, 1u);
  CHECK(result.value().differences.front().has_expected);
  CHECK(result.value().differences.front().has_observed);
}

PATHOBS_TEST(compare, an_unknown_route_generation_is_not_compared) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  request.generation = RouteGenerationId::from_value(4242);
  request.evidence.clear();
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK_EQ(result.value().primary, DivergenceClass::RouteGenerationMismatch);
  CHECK(!result.value().is_compliance());
}

PATHOBS_TEST(compare, a_subject_the_plan_does_not_contain_has_no_expected_state) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  request.group = MaybeId<PathGroupId>(PathGroupId::from_value(777));
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::NoExpectedState);
  CHECK(!result.value().is_compliance());
}

PATHOBS_TEST(compare, legitimate_multipath_is_not_divergence) {
  synthetic::LabOptions lab;
  lab.multipath_members = 3;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  CHECK(harness.value().fabric.routes.groups.front().members.size() == 3u);

  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Match);
  CHECK(result.value().is_compliance());
  CHECK(result.value().multipath);
  CHECK(!result.value().has_class(DivergenceClass::DeviceMismatch));

  // Evidence that follows a different member of the same group is still
  // compliance, not divergence.
  ObservedPathEvidence alternate = harness.value().fabric.evidence.front();
  const ExpectedPath& second = harness.value().fabric.routes.paths[1];
  alternate.hops.clear();
  for (const auto& hop : second.hops) {
    ObservedHop observed;
    observed.index = hop.index;
    observed.device = hop.device;
    observed.ingress = hop.ingress;
    observed.egress = hop.egress;
    alternate.hops.push_back(observed);
  }
  alternate.path = MaybeId<PathId>(second.id);
  alternate.seal();

  ComparisonRequest alternate_request = make_request(harness.value());
  alternate_request.evidence = {alternate};
  const auto alternate_result = run(harness.value(), alternate_request);
  REQUIRE(alternate_result.has_value());
  CHECK_EQ(alternate_result.value().outcome, MatchOutcome::Match);
  CHECK(!alternate_result.value().has_class(DivergenceClass::DeviceMismatch));
  CHECK(alternate_result.value().matched_path.present());
  CHECK_EQ(alternate_result.value().matched_path.value().value(), second.id.value());
}

PATHOBS_TEST(compare, conflicting_records_about_one_path_are_indeterminate) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());

  ObservedPathEvidence conflicting = harness.value().fabric.evidence.front();
  conflicting.hops[1].device = synthetic::divergent_device_id();
  conflicting.sequence = SourceSequence::from_value(99);
  conflicting.seal();

  ComparisonRequest request = make_request(harness.value());
  request.evidence = {harness.value().fabric.evidence.front(), conflicting};
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
  CHECK_EQ(result.value().consistency, ObservationConsistency::Conflicting);
  CHECK(result.value().has_class(DivergenceClass::EvidenceConflicting));
  CHECK(!result.value().is_compliance());
}

PATHOBS_TEST(compare, duplicate_records_do_not_manufacture_agreement) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  request.evidence.push_back(harness.value().fabric.evidence.front());
  request.evidence.push_back(harness.value().fabric.evidence.front());
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().evidence_considered, 1u);
  CHECK_EQ(result.value().duplicate_evidence, 2u);
  CHECK_EQ(result.value().outcome, MatchOutcome::Match);
}

PATHOBS_TEST(compare, an_unsupported_proof_surface_never_becomes_compliance) {
  synthetic::LabOptions lab;
  lab.surface = ProofSurface::Unsupported;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Unsupported);
  CHECK(!result.value().is_compliance());
  CHECK_EQ(result.value().surface, ProofSurface::Unsupported);
}

PATHOBS_TEST(compare, a_sequence_gap_downgrades_completeness) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  request.source_sequence_gap = true;
  const auto result = run(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK(result.value().has_class(DivergenceClass::ObservationGap));
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Partial);
  CHECK_EQ(result.value().outcome, MatchOutcome::Indeterminate);
}

PATHOBS_TEST(compare, missing_link_identity_makes_the_observation_incomplete) {
  synthetic::LabOptions lab;
  lab.omit_links = true;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  ComparisonRequest request = make_request(harness.value());
  // Remove the topology index so correlation cannot happen; the plan's declared
  // link identity then has nothing to match against.
  bind(harness.value());
  harness.value().context.topology_index = nullptr;
  const auto result = evaluate(harness.value(), request);
  REQUIRE(result.has_value());
  CHECK(result.value().has_class(DivergenceClass::LinkUnresolved));
  CHECK_EQ(result.value().completeness, EvidenceCompleteness::Partial);
  CHECK(!result.value().is_compliance());
}

PATHOBS_TEST(compare, evidence_from_an_unregistered_source_is_rejected) {
  synthetic::LabOptions lab;
  auto harness = make_harness(lab);
  REQUIRE(harness.has_value());
  SourceRegistry registry;
  harness.value().context.sources = &registry;
  const auto result = run(harness.value(), make_request(harness.value()));
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().evidence_considered, 0u);
  CHECK(result.value().has_class(DivergenceClass::AuthorityInsufficient));
  CHECK(!result.value().is_compliance());

  SourceDescriptor descriptor = harness.value().fabric.sources[1];
  REQUIRE(registry.register_source(descriptor).has_value());
  const auto registered = run(harness.value(), make_request(harness.value()));
  REQUIRE(registered.has_value());
  CHECK_EQ(registered.value().outcome, MatchOutcome::Match);
}
