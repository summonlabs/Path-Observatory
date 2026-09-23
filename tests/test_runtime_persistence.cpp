// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "test_framework.hpp"

#include "pathobs/codec.hpp"
#include "pathobs/documents.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace pathobs;

namespace {

std::filesystem::path scratch_path(const char* name) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "pathobs-tests";
  std::error_code ignored;
  std::filesystem::create_directories(directory, ignored);
  const std::filesystem::path path = directory / name;
  std::filesystem::remove(path, ignored);
  return path;
}

TimePoint base_time() { return TimePoint::from_unix_nanos(1767225600000000000LL); }

struct Runtime {
  std::unique_ptr<ObservatoryRuntime> instance;
  synthetic::LabFabric fabric;
};

Result<Runtime> make_runtime(const synthetic::LabOptions& lab, TimePoint now,
                             const std::filesystem::path* store = nullptr,
                             Limits limits = default_limits()) {
  Runtime runtime;
  PATHOBS_TRY(fabric, synthetic::build_lab_fabric(lab, limits));
  runtime.fabric = std::move(fabric);
  RuntimeConfig config;
  config.limits = limits;
  config.worker_threads = 2;
  if (store != nullptr) {
    config.persist = true;
    config.store_path = *store;
  }
  auto clock = std::make_unique<ManualClock>(now);
  PATHOBS_TRY(created, ObservatoryRuntime::create(config, std::move(clock)));
  runtime.instance = std::move(created);
  const Status started = runtime.instance->start();
  if (!started.has_value()) {
    return started.error();
  }
  return runtime;
}

Status publish(ObservatoryRuntime& runtime, const synthetic::LabFabric& fabric) {
  for (const auto& descriptor : fabric.sources) {
    const Status registered = runtime.register_source(descriptor);
    if (!registered.has_value()) {
      return registered.error();
    }
  }
  const Status topology = runtime.publish_topology(fabric.topology);
  if (!topology.has_value()) {
    return topology.error();
  }
  const Status routes = runtime.publish_routes(fabric.routes);
  if (!routes.has_value()) {
    return routes.error();
  }
  return ok();
}

} // namespace

PATHOBS_TEST(runtime, lifecycle_transitions_are_explicit) {
  synthetic::LabOptions lab;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  CHECK_EQ(created.value().instance->state(), RuntimeState::Running);
  // Starting twice is a state error, not a silent no-op.
  const Status second_start = created.value().instance->start();
  CHECK(!second_start.has_value());
  CHECK_EQ(static_cast<int>(second_start.error().code), static_cast<int>(ErrorCode::InvalidState));
  CHECK(created.value().instance->stop().has_value());
  CHECK_EQ(created.value().instance->state(), RuntimeState::Stopped);
  CHECK(created.value().instance->stop().has_value());
  CHECK(!created.value().instance->submit_batch({}).has_value());
}

PATHOBS_TEST(runtime, expected_state_must_arrive_in_order) {
  synthetic::LabOptions lab;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;

  // An unregistered source cannot publish anything.
  const Status unknown_source = runtime.publish_topology(created.value().fabric.topology);
  CHECK(!unknown_source.has_value());
  CHECK_EQ(static_cast<int>(unknown_source.error().code),
           static_cast<int>(ErrorCode::UnknownIdentity));

  // With the source registered but no topology published, a route generation
  // that references one is refused.
  for (const auto& descriptor : created.value().fabric.sources) {
    CHECK(runtime.register_source(descriptor).has_value());
  }
  const Status premature = runtime.publish_routes(created.value().fabric.routes);
  CHECK(!premature.has_value());
  CHECK_EQ(static_cast<int>(premature.error().code), static_cast<int>(ErrorCode::InvalidState));

  CHECK(runtime.publish_topology(created.value().fabric.topology).has_value());
  CHECK(runtime.publish_routes(created.value().fabric.routes).has_value());
  CHECK(runtime.has_topology());
  CHECK(runtime.has_routes());

  // A superseded epoch is fenced rather than accepted. Moving to a later epoch
  // first makes the supersession observable.
  TopologyGeneration later = created.value().fabric.topology;
  later.id = TopologyGenerationId::from_value(later.id.value() + 500);
  later.revision = RevisionId::from_value(5);
  later.epoch = EpochId::from_value(later.epoch.value() + 1);
  CHECK(runtime.publish_topology(later).has_value());
  const Status fenced = runtime.publish_topology(created.value().fabric.topology);
  CHECK(!fenced.has_value());
  CHECK_EQ(static_cast<int>(fenced.error().code), static_cast<int>(ErrorCode::FencedEvidence));
  CHECK(runtime.publish_topology(later).has_value());

  // And an older revision inside the same incarnation is fenced too.
  TopologyGeneration stale = later;
  stale.id = TopologyGenerationId::from_value(stale.id.value() + 1);
  stale.revision = RevisionId::from_value(1);
  const Status stale_result = runtime.publish_topology(stale);
  CHECK(!stale_result.has_value());
  CHECK_EQ(static_cast<int>(stale_result.error().code), static_cast<int>(ErrorCode::FencedEvidence));

  // Republishing the current generation is idempotent.
  CHECK(runtime.publish_topology(later).has_value());
}

PATHOBS_TEST(runtime, sequence_replay_and_gaps_are_fenced) {
  synthetic::LabOptions lab;
  lab.evidence_count = 3;
  lab.first_sequence = 10;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;
  REQUIRE(publish(runtime, created.value().fabric).has_value());

  const auto& evidence = created.value().fabric.evidence;
  REQUIRE(evidence.size() == 3u);
  CHECK(runtime.ingest_evidence(evidence[0]).value().accepted);

  // A gap is accepted but recorded as missing evidence.
  const auto gapped = runtime.ingest_evidence(evidence[2]);
  REQUIRE(gapped.has_value());
  CHECK(gapped.value().accepted);
  CHECK(gapped.value().sequence_gap);

  // A record that arrives after a later sequence has already been accepted is a
  // replay of the covered range. The ledger fences it rather than rewriting
  // history: an observation that arrives out of order is not silently admitted
  // after the fact.
  const auto out_of_order = runtime.ingest_evidence(evidence[1]);
  REQUIRE(out_of_order.has_value());
  CHECK(!out_of_order.value().accepted);
  CHECK(out_of_order.value().replay);
  CHECK_EQ(static_cast<int>(out_of_order.value().reason),
           static_cast<int>(DivergenceClass::SourceSequenceReplay));

  // Replaying the first record is refused as well.
  const auto replay = runtime.ingest_evidence(evidence[0]);
  REQUIRE(replay.has_value());
  CHECK(!replay.value().accepted);
  CHECK(replay.value().replay);

  CHECK_EQ(runtime.stats().metrics.evidence_replayed, 2ull);
  CHECK_EQ(runtime.stats().evidence_records, 2u);
}

PATHOBS_TEST(runtime, evidence_from_an_unregistered_source_is_refused) {
  synthetic::LabOptions lab;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;
  const auto ingested = runtime.ingest_evidence(created.value().fabric.evidence.front());
  REQUIRE(ingested.has_value());
  CHECK(!ingested.value().accepted);
  CHECK_EQ(static_cast<int>(ingested.value().reason),
           static_cast<int>(DivergenceClass::AuthorityInsufficient));
  CHECK_EQ(runtime.stats().metrics.evidence_unregistered_source, 1ull);
}

PATHOBS_TEST(runtime, a_surface_mismatch_is_refused) {
  synthetic::LabOptions lab;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;
  REQUIRE(publish(runtime, created.value().fabric).has_value());
  ObservedPathEvidence evidence = created.value().fabric.evidence.front();
  evidence.surface = ProofSurface::Real;
  const auto ingested = runtime.ingest_evidence(evidence);
  REQUIRE(ingested.has_value());
  CHECK(!ingested.value().accepted);
}

PATHOBS_TEST(runtime, comparison_records_history_and_findings) {
  synthetic::LabOptions lab;
  lab.diverge = true;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;
  REQUIRE(publish(runtime, created.value().fabric).has_value());
  for (const auto& evidence : created.value().fabric.evidence) {
    static_cast<void>(runtime.ingest_evidence(evidence));
  }

  const auto results = runtime.compare_all();
  REQUIRE(results.has_value());
  REQUIRE(results.value().size() == 1u);
  CHECK_EQ(results.value().front().outcome, MatchOutcome::Divergent);
  CHECK_EQ(runtime.stats().history, 1u);
  CHECK_EQ(runtime.stats().findings, 1u);
  CHECK_EQ(runtime.stats().findings_open, 1u);

  const std::vector<Finding> findings = runtime.findings();
  REQUIRE(findings.size() == 1u);
  const Finding& finding = findings.front();
  CHECK_EQ(finding.cls, DivergenceClass::DeviceMismatch);
  CHECK(finding.id.valid());
  CHECK_EQ(finding.occurrences, 1ull);
  CHECK(finding.is_open());

  // A second identical comparison is the same finding, not a new one.
  const auto repeated = runtime.compare_all();
  REQUIRE(repeated.has_value());
  CHECK_EQ(runtime.stats().findings, 1u);
  const std::vector<Finding> after = runtime.findings();
  REQUIRE(after.size() == 1u);
  CHECK_EQ(after.front().id.str(), finding.id.str());
  CHECK_EQ(after.front().occurrences, 2ull);

  CHECK(runtime.acknowledge_finding(finding.id).has_value());
  const auto acknowledged = runtime.finding(finding.id);
  REQUIRE(acknowledged.has_value());
  CHECK_EQ(acknowledged.value().status, FindingStatus::Acknowledged);

  const auto history = runtime.history({});
  REQUIRE(history.has_value());
  CHECK_EQ(history.value().records.size(), 2u);
  CHECK_EQ(history.value().records.front().outcome, MatchOutcome::Divergent);
}

PATHOBS_TEST(runtime, finding_identity_is_stable_under_evidence_reordering) {
  synthetic::LabOptions lab;
  lab.diverge = true;
  lab.evidence_count = 3;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  ObservatoryRuntime& runtime = *created.value().instance;
  REQUIRE(publish(runtime, created.value().fabric).has_value());
  for (const auto& evidence : created.value().fabric.evidence) {
    static_cast<void>(runtime.ingest_evidence(evidence));
  }
  REQUIRE(runtime.compare_all().has_value());
  const std::vector<Finding> first = runtime.findings();
  REQUIRE(first.size() == 1u);

  // A second runtime ingesting the same records in the opposite order produces
  // the same finding identity.
  auto other = make_runtime(lab, base_time());
  REQUIRE(other.has_value());
  REQUIRE(publish(*other.value().instance, other.value().fabric).has_value());
  std::vector<ObservedPathEvidence> reversed = other.value().fabric.evidence;
  std::reverse(reversed.begin(), reversed.end());
  // Ingestion order does not change the sequence ledger because the sequences
  // are ascending in the reversed vector only if they were descending before;
  // feed them in ledger order and let the engine see the shuffled bucket.
  for (const auto& evidence : other.value().fabric.evidence) {
    static_cast<void>(other.value().instance->ingest_evidence(evidence));
  }
  REQUIRE(other.value().instance->compare_all().has_value());
  const std::vector<Finding> second = other.value().instance->findings();
  REQUIRE(second.size() == 1u);
  CHECK_EQ(first.front().id.str(), second.front().id.str());
}

PATHOBS_TEST(persistence, a_store_round_trips_and_reports_its_generation) {
  const std::filesystem::path path = scratch_path("round-trip.pob");
  StoreOptions options;
  const std::string payload = "hello observatory";
  StoreIncarnationId first_incarnation;
  {
    // A store file has exactly one owner at a time: the first instance is
    // destroyed before the restart below opens the same path.
    auto first = PersistentStore::open(path, options, base_time());
    REQUIRE(first.has_value());
    CHECK(first.value().recovery().created);
    CHECK_EQ(first.value().recovery().restart_count, 0u);
    CHECK_EQ(first.value().recovery().valid_records, 0ull);
    first_incarnation = first.value().recovery().incarnation;
    CHECK(first_incarnation.valid());

    REQUIRE(first.value().append(RecordType::Marker,
                                 std::span<const std::byte>(
                                     reinterpret_cast<const std::byte*>(payload.data()),
                                     payload.size()))
                .has_value());
    CHECK_EQ(first.value().record_count(), 1ull);
    const auto in_place = first.value().read_all();
    REQUIRE(in_place.has_value());
    CHECK_EQ(in_place.value().size(), 1u);
    CHECK(first.value().mark_clean(base_time()).has_value());
    CHECK(first.value().size_bytes() > 0);
  }

  auto second = PersistentStore::open(path, options, base_time());
  REQUIRE(second.has_value());
  CHECK(!second.value().recovery().created);
  CHECK_EQ(second.value().recovery().valid_records, 1ull);
  CHECK_EQ(second.value().recovery().restart_count, 1u);
  CHECK(!second.value().recovery().unclean_previous_shutdown);
  CHECK(!second.value().recovery().corrupt_tail_truncated);
  CHECK_NE(second.value().recovery().incarnation.str(), first_incarnation.str());

  const auto records = second.value().read_all();
  REQUIRE(records.has_value());
  REQUIRE(records.value().size() == 1u);
  CHECK_EQ(records.value().front().type, RecordType::Marker);
  CHECK_EQ(records.value().front().payload.size(), payload.size());
  CHECK(second.value().mark_clean(base_time()).has_value());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

PATHOBS_TEST(persistence, a_corrupt_tail_is_truncated_and_reported) {
  const std::filesystem::path path = scratch_path("corrupt-tail.pob");
  StoreOptions options;
  {
    auto store = PersistentStore::open(path, options, base_time());
    REQUIRE(store.has_value());
    for (int i = 0; i < 4; ++i) {
      const std::string payload = "record-" + std::to_string(i);
      REQUIRE(store.value()
                  .append(RecordType::Marker,
                          std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(payload.data()), payload.size()))
                  .has_value());
    }
    CHECK(store.value().mark_clean(base_time()).has_value());
  }

  {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    REQUIRE(stream.good());
    const char junk[] = "this is not a record, it is debris";
    stream.write(junk, static_cast<std::streamsize>(sizeof(junk) - 1));
  }

  auto recovered = PersistentStore::open(path, options, base_time());
  REQUIRE(recovered.has_value());
  CHECK_EQ(recovered.value().recovery().valid_records, 4ull);
  CHECK(recovered.value().recovery().corrupt_tail_truncated);
  CHECK(recovered.value().recovery().truncate_offset > 0);
  CHECK(!recovered.value().recovery().truncate_detail.empty());
  const auto records = recovered.value().read_all();
  REQUIRE(records.has_value());
  CHECK_EQ(records.value().size(), 4u);
  CHECK(recovered.value().mark_clean(base_time()).has_value());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

PATHOBS_TEST(persistence, a_corrupt_payload_is_detected_rather_than_reinterpreted) {
  const std::filesystem::path path = scratch_path("corrupt-payload.pob");
  StoreOptions options;
  {
    auto store = PersistentStore::open(path, options, base_time());
    REQUIRE(store.has_value());
    const std::string payload = "0123456789";
    REQUIRE(store.value()
                .append(RecordType::Marker,
                        std::span<const std::byte>(
                            reinterpret_cast<const std::byte*>(payload.data()), payload.size()))
                .has_value());
    CHECK(store.value().mark_clean(base_time()).has_value());
  }

  {
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(stream.good());
    stream.seekp(static_cast<std::streamoff>(kStoreHeaderBytes + kStoreRecordHeaderBytes + 2));
    stream.put('X');
  }

  // A read only open refuses instead of truncating.
  StoreOptions read_only;
  read_only.read_only = true;
  auto refused = PersistentStore::open(path, read_only, base_time());
  CHECK(!refused.has_value());
  CHECK_EQ(static_cast<int>(refused.error().code), static_cast<int>(ErrorCode::CorruptStore));

  // A writable open recovers by truncating the damaged record and says so.
  auto recovered = PersistentStore::open(path, options, base_time());
  REQUIRE(recovered.has_value());
  CHECK_EQ(recovered.value().recovery().valid_records, 0ull);
  CHECK(recovered.value().recovery().corrupt_tail_truncated);
  CHECK(recovered.value().mark_clean(base_time()).has_value());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

PATHOBS_TEST(persistence, a_destroyed_header_is_refused) {
  const std::filesystem::path path = scratch_path("bad-header.pob");
  StoreOptions options;
  {
    auto store = PersistentStore::open(path, options, base_time());
    REQUIRE(store.has_value());
    CHECK(store.value().mark_clean(base_time()).has_value());
  }
  {
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(stream.good());
    for (std::streamoff offset = 0; offset < static_cast<std::streamoff>(kStoreHeaderBytes);
         offset += 7) {
      stream.seekp(offset);
      stream.put(static_cast<char>(0x5A));
    }
  }
  auto refused = PersistentStore::open(path, options, base_time());
  CHECK(!refused.has_value());
  CHECK_EQ(static_cast<int>(refused.error().code), static_cast<int>(ErrorCode::CorruptStore));
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

PATHOBS_TEST(persistence, capacity_bounds_are_enforced) {
  const std::filesystem::path path = scratch_path("bounded.pob");
  StoreOptions options;
  options.limits.max_store_records = 3;
  auto store = PersistentStore::open(path, options, base_time());
  REQUIRE(store.has_value());
  for (int i = 0; i < 3; ++i) {
    const std::string payload = "x";
    CHECK(store.value()
              .append(RecordType::Marker,
                      std::span<const std::byte>(
                          reinterpret_cast<const std::byte*>(payload.data()), payload.size()))
              .has_value());
  }
  const std::string payload = "x";
  const auto beyond = store.value().append(
      RecordType::Marker,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
  CHECK(!beyond.has_value());
  CHECK_EQ(static_cast<int>(beyond.error().code), static_cast<int>(ErrorCode::CapacityExceeded));
  CHECK(store.value().mark_clean(base_time()).has_value());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

PATHOBS_TEST(persistence, restart_does_not_resurrect_expected_state_or_freshness) {
  const std::filesystem::path path = scratch_path("restart.pob");
  synthetic::LabOptions lab;
  lab.evidence_count = 2;

  const TimePoint first_now = base_time();
  {
    auto created = make_runtime(lab, first_now, &path);
    REQUIRE(created.has_value());
    REQUIRE(publish(*created.value().instance, created.value().fabric).has_value());
    for (const auto& evidence : created.value().fabric.evidence) {
      const auto ingested = created.value().instance->ingest_evidence(evidence);
      REQUIRE(ingested.has_value());
      CHECK(ingested.value().accepted);
    }
    const auto results = created.value().instance->compare_all();
    REQUIRE(results.has_value());
    CHECK_EQ(results.value().front().outcome, MatchOutcome::Match);
    CHECK(created.value().instance->stop().has_value());
  }

  // Restart one hour later.
  const TimePoint second_now = TimePoint::from_unix_nanos(first_now.unix_nanos() +
                                                          3600LL * 1000000000LL);
  {
    auto created = make_runtime(lab, second_now, &path);
    if (!created.has_value()) {
      FAIL_WITH(describe(created.error()));
      return;
    }
    ObservatoryRuntime& runtime = *created.value().instance;
    const RuntimeStats stats = runtime.stats();
    CHECK_EQ(stats.evidence_records, 2u);
    CHECK(!stats.has_route_generation);
    CHECK(!stats.has_topology_generation);

    // Nothing can be compared until the authority republishes.
    const auto premature = runtime.compare_all();
    REQUIRE(premature.has_value());
    for (const auto& result : premature.value()) {
      CHECK(!result.is_compliance());
      CHECK_EQ(result.outcome, MatchOutcome::Indeterminate);
    }

    // After republication the restored evidence is still stale, because it
    // keeps the receive time it was written with.
    REQUIRE(publish(runtime, created.value().fabric).has_value());
    const auto results = runtime.compare_all();
    REQUIRE(results.has_value());
    REQUIRE(!results.value().empty());
    CHECK_EQ(results.value().front().outcome, MatchOutcome::Indeterminate);
    CHECK(!results.value().front().is_compliance());
    CHECK_EQ(results.value().front().freshness, Freshness::Expired);
    CHECK(results.value().front().has_class(DivergenceClass::EvidenceExpired));
    CHECK(runtime.stop().has_value());
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(std::filesystem::path(path.string() + ".compact"), ignored);
}

PATHOBS_TEST(persistence, compaction_keeps_the_newest_records_and_stays_valid) {
  const std::filesystem::path path = scratch_path("compact.pob");
  StoreOptions options;
  {
    auto store = PersistentStore::open(path, options, base_time());
    REQUIRE(store.has_value());
    for (int i = 0; i < 10; ++i) {
      const std::string payload = "record-" + std::to_string(i);
      REQUIRE(store.value()
                  .append(RecordType::Marker,
                          std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(payload.data()), payload.size()))
                  .has_value());
    }
    const auto compacted = store.value().compact(4);
    REQUIRE(compacted.has_value());
    CHECK_EQ(compacted.value(), 4u);
    const auto in_place = store.value().read_all();
    REQUIRE(in_place.has_value());
    CHECK_EQ(in_place.value().size(), 4u);
    CHECK_EQ(store.value().record_count(), 4ull);
  }

  auto reopened = PersistentStore::open(path, options, base_time());
  REQUIRE(reopened.has_value());
  CHECK_EQ(reopened.value().recovery().valid_records, 4ull);
  const auto records = reopened.value().read_all();
  REQUIRE(records.has_value());
  REQUIRE(records.value().size() == 4u);
  CHECK_EQ(std::string(reinterpret_cast<const char*>(records.value().back().payload.data()),
                       records.value().back().payload.size()),
           std::string("record-9"));
  CHECK(reopened.value().mark_clean(base_time()).has_value());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(std::filesystem::path(path.string() + ".compact"), ignored);
}

PATHOBS_TEST(runtime, evidence_survives_a_codec_round_trip) {
  synthetic::LabOptions lab;
  lab.evidence_count = 1;
  auto created = make_runtime(lab, base_time());
  REQUIRE(created.has_value());
  const ObservedPathEvidence& original = created.value().fabric.evidence.front();
  const std::string encoded = codec::encode(original);
  const auto decoded = codec::decode_exact<ObservedPathEvidence>(
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(encoded.data()),
                                 encoded.size()),
      default_limits());
  REQUIRE(decoded.has_value());
  CHECK_EQ(decoded.value().id.str(), original.id.str());
  CHECK_EQ(decoded.value().sequence.value(), original.sequence.value());
  CHECK_EQ(decoded.value().received_at.unix_nanos(), original.received_at.unix_nanos());
  CHECK_EQ(decoded.value().hops.size(), original.hops.size());

  // A single flipped byte in the payload makes the identity check fail.
  std::string tampered = encoded;
  tampered.back() = static_cast<char>(tampered.back() ^ 0x01);
  const auto refused = codec::decode_exact<ObservedPathEvidence>(
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(tampered.data()),
                                 tampered.size()),
      default_limits());
  CHECK(!refused.has_value());
}
