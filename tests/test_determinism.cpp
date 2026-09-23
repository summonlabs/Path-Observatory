// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Determinism, property and adversarial coverage of the comparison engine.
//
// The defining property is: the canonical digest of a comparison result is a
// function of the SET of evidence records, not of their order, not of how many
// times a record is repeated, and not of the iteration order of any container.

#include "test_framework.hpp"

#include "pathobs/compare.hpp"
#include "pathobs/random.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/topology.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace pathobs;

namespace {

struct Scenario {
  synthetic::LabFabric fabric;
  TopologyIndex index;
  ComparisonContext context;
};

/// The context points into the fabric, so it is bound after the scenario has
/// come to rest rather than inside the factory that builds it.
void bind(Scenario& scenario) {
  scenario.context.routes = &scenario.fabric.routes;
  scenario.context.topology = &scenario.fabric.topology;
  scenario.context.topology_index = &scenario.index;
  scenario.context.authority.epoch = scenario.fabric.routes.epoch;
  scenario.context.authority.incarnation = scenario.fabric.routes.incarnation;
  scenario.context.authority.route_revision = scenario.fabric.routes.revision;
  scenario.context.authority.topology_revision = scenario.fabric.topology.revision;
  scenario.context.now = scenario.fabric.routes.received;
}

Result<Scenario> make_scenario(const synthetic::LabOptions& options) {
  Scenario scenario;
  PATHOBS_TRY(fabric, synthetic::build_lab_fabric(options, default_limits()));
  scenario.fabric = std::move(fabric);
  PATHOBS_TRY(index, TopologyIndex::build(scenario.fabric.topology, default_limits()));
  scenario.index = std::move(index);
  return scenario;
}

Result<ComparisonResult> evaluate(Scenario& scenario,
                                  const std::vector<ObservedPathEvidence>& evidence) {
  bind(scenario);
  ComparisonRequest request;
  request.generation = scenario.fabric.routes.id;
  request.group = MaybeId<PathGroupId>(scenario.fabric.group);
  request.evidence = evidence;
  const ComparisonEngine engine(default_limits(), ComparisonPolicy{});
  return engine.compare(request, scenario.context);
}

} // namespace

PATHOBS_TEST(determinism, permuting_evidence_does_not_change_the_result) {
  synthetic::LabOptions lab;
  lab.evidence_count = 6;
  auto scenario = make_scenario(lab);
  REQUIRE(scenario.has_value());

  std::vector<ObservedPathEvidence> baseline = scenario.value().fabric.evidence;
  const auto reference = evaluate(scenario.value(), baseline);
  REQUIRE(reference.has_value());

  Rng rng(20260214);
  for (int round = 0; round < 64; ++round) {
    std::vector<ObservedPathEvidence> shuffled = baseline;
    rng.shuffle(shuffled);
    const auto result = evaluate(scenario.value(), shuffled);
    REQUIRE(result.has_value());
    CHECK_EQ(result.value().canonical_digest().hex(),
             reference.value().canonical_digest().hex());
    CHECK_EQ(result.value().id.str(), reference.value().id.str());
    CHECK_EQ(static_cast<int>(result.value().outcome),
             static_cast<int>(reference.value().outcome));
  }

  // Reversal and duplication are also order effects that must not matter.
  std::vector<ObservedPathEvidence> reversed(baseline.rbegin(), baseline.rend());
  const auto reversed_result = evaluate(scenario.value(), reversed);
  REQUIRE(reversed_result.has_value());
  CHECK_EQ(reversed_result.value().canonical_digest().hex(),
           reference.value().canonical_digest().hex());

  std::vector<ObservedPathEvidence> duplicated = baseline;
  duplicated.insert(duplicated.end(), baseline.begin(), baseline.end());
  const auto duplicated_result = evaluate(scenario.value(), duplicated);
  REQUIRE(duplicated_result.has_value());
  CHECK_EQ(duplicated_result.value().canonical_digest().hex(),
           reference.value().canonical_digest().hex());
}

PATHOBS_TEST(determinism, reordering_conflicting_evidence_picks_the_same_class) {
  synthetic::LabOptions lab;
  auto scenario = make_scenario(lab);
  REQUIRE(scenario.has_value());

  ObservedPathEvidence diverging = scenario.value().fabric.evidence.front();
  diverging.hops[1].device = synthetic::divergent_device_id();
  diverging.sequence = SourceSequence::from_value(50);
  diverging.seal();

  ObservedPathEvidence stale = scenario.value().fabric.evidence.front();
  stale.epoch = EpochId::from_value(scenario.value().fabric.routes.epoch.value() - 1);
  stale.sequence = SourceSequence::from_value(60);
  stale.seal();

  std::vector<ObservedPathEvidence> forward = {scenario.value().fabric.evidence.front(), diverging,
                                               stale};
  std::vector<ObservedPathEvidence> backward = {stale, diverging,
                                                scenario.value().fabric.evidence.front()};

  const auto first = evaluate(scenario.value(), forward);
  const auto second = evaluate(scenario.value(), backward);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK_EQ(first.value().canonical_digest().hex(), second.value().canonical_digest().hex());
  CHECK_EQ(static_cast<int>(first.value().primary), static_cast<int>(second.value().primary));
  CHECK_EQ(static_cast<int>(first.value().outcome), static_cast<int>(second.value().outcome));
}

PATHOBS_TEST(determinism, randomized_scenarios_are_reproducible_from_their_seed) {
  const Limits limits = default_limits();
  for (std::uint64_t seed = 1; seed <= 24; ++seed) {
    Rng rng(seed);
    synthetic::LabOptions lab;
    lab.seed = seed;
    lab.hop_count = static_cast<std::uint32_t>(2 + rng.next_below(6));
    lab.multipath_members = static_cast<std::uint32_t>(1 + rng.next_below(3));
    lab.evidence_count = static_cast<std::uint32_t>(1 + rng.next_below(5));
    lab.partial = rng.next_bool();
    lab.omit_links = rng.next_bool();
    lab.diverge = rng.next_bool();
    lab.evidence_age_nanos = static_cast<std::int64_t>(rng.next_below(4)) * 1000000000LL;

    auto scenario = make_scenario(lab);
    REQUIRE(scenario.has_value());
    const auto first = evaluate(scenario.value(), scenario.value().fabric.evidence);
    REQUIRE(first.has_value());

    // Rebuild from the same options: the generator and the engine must both be
    // pure functions of their inputs.
    auto rebuilt = make_scenario(lab);
    REQUIRE(rebuilt.has_value());
    const auto second = evaluate(rebuilt.value(), rebuilt.value().fabric.evidence);
    REQUIRE(second.has_value());
    CHECK_EQ(first.value().canonical_digest().hex(), second.value().canonical_digest().hex());

    // And the same set in a different order must agree with itself.
    std::vector<ObservedPathEvidence> shuffled = rebuilt.value().fabric.evidence;
    rng.shuffle(shuffled);
    const auto third = evaluate(rebuilt.value(), shuffled);
    REQUIRE(third.has_value());
    CHECK_EQ(first.value().canonical_digest().hex(), third.value().canonical_digest().hex());

    // The invariant that matters most: compliance implies fresh, complete,
    // consistent evidence of a judged surface.
    for (const auto* result : {&first.value(), &second.value(), &third.value()}) {
      if (result->is_compliance()) {
        CHECK_EQ(result->freshness, Freshness::Fresh);
        CHECK_EQ(result->completeness, EvidenceCompleteness::Complete);
        CHECK(result->consistency != ObservationConsistency::Conflicting);
        CHECK(result->surface != ProofSurface::Unsupported);
        CHECK(result->surface != ProofSurface::Unknown);
        CHECK(result->evidence_considered > 0);
        CHECK(result->primary == DivergenceClass::None);
      }
    }
  }
  static_cast<void>(limits);
}

PATHOBS_TEST(determinism, the_generator_is_a_pure_function_of_its_options) {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.seed = 99;
  lab.hop_count = 5;
  lab.multipath_members = 2;
  lab.evidence_count = 3;
  const auto first = synthetic::build_lab_fabric(lab, limits);
  const auto second = synthetic::build_lab_fabric(lab, limits);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK_EQ(first.value().topology.digest.hex(), second.value().topology.digest.hex());
  CHECK_EQ(first.value().routes.digest.hex(), second.value().routes.digest.hex());
  REQUIRE(first.value().evidence.size() == second.value().evidence.size());
  for (std::size_t i = 0; i < first.value().evidence.size(); ++i) {
    CHECK_EQ(first.value().evidence[i].id.str(), second.value().evidence[i].id.str());
  }
}

PATHOBS_TEST(determinism, evidence_identity_excludes_receive_time) {
  synthetic::LabOptions lab;
  const auto scenario = make_scenario(lab);
  REQUIRE(scenario.has_value());
  ObservedPathEvidence later = scenario.value().fabric.evidence.front();
  later.received_at = TimePoint::from_unix_nanos(later.received_at.unix_nanos() +
                                                 5000000000LL);
  later.seal();
  CHECK_EQ(later.id.str(), scenario.value().fabric.evidence.front().id.str());

  // But any change to what was observed does change the identity.
  ObservedPathEvidence changed = scenario.value().fabric.evidence.front();
  changed.hops.front().device = DeviceId::from_value(123456);
  changed.seal();
  CHECK_NE(changed.id.str(), scenario.value().fabric.evidence.front().id.str());
}

PATHOBS_TEST(determinism, adversarial_evidence_shapes_are_refused_not_guessed) {
  synthetic::LabOptions lab;
  auto scenario = make_scenario(lab);
  REQUIRE(scenario.has_value());

  // Descending hop indices are a shape error, so the record is excluded.
  ObservedPathEvidence descending = scenario.value().fabric.evidence.front();
  if (descending.hops.size() >= 2) {
    std::swap(descending.hops[0].index, descending.hops[1].index);
  }
  // Resealing gives the mutated record its own identity; without it the record
  // would be rejected as an identity mismatch rather than as a shape error.
  descending.seal();
  std::vector<ObservedPathEvidence> with_descending = {scenario.value().fabric.evidence.front(),
                                                       descending};
  const auto result = evaluate(scenario.value(), with_descending);
  REQUIRE(result.has_value());
  CHECK_EQ(result.value().outcome, MatchOutcome::Match);
  CHECK_EQ(result.value().evidence_considered, 1u);
  CHECK_EQ(result.value().evidence_rejected, 1u);
  CHECK(result.value().has_class(DivergenceClass::EvidenceUnknown));

  // A record that names a link without saying how it was obtained is refused.
  ObservedPathEvidence link_without_provenance = scenario.value().fabric.evidence.front();
  link_without_provenance.hops.front().link = LinkId::from_value(1000);
  link_without_provenance.hops.front().link_provenance = HopLinkProvenance::Absent;
  const std::vector<ObservedPathEvidence> bad = {link_without_provenance};
  const auto refused = evaluate(scenario.value(), bad);
  REQUIRE(refused.has_value());
  CHECK_EQ(refused.value().evidence_considered, 0u);
  CHECK(!refused.value().is_compliance());
}

PATHOBS_TEST(determinism, an_oversized_request_is_refused_before_processing) {
  synthetic::LabOptions lab;
  auto scenario = make_scenario(lab);
  REQUIRE(scenario.has_value());
  Limits limits = default_limits();
  ComparisonRequest request;
  request.generation = scenario.value().fabric.routes.id;
  request.group = MaybeId<PathGroupId>(scenario.value().fabric.group);
  request.evidence.resize(limits.max_evidence_per_comparison + 1);
  const ComparisonEngine engine(limits, ComparisonPolicy{});
  const auto result = engine.compare(request, scenario.value().context);
  CHECK(!result.has_value());
  CHECK_EQ(static_cast<int>(result.error().code),
           static_cast<int>(ErrorCode::CapacityExceeded));
}
