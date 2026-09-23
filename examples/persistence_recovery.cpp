// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Persist evidence, stop, restart and observe that the evidence is still stale.
//
// PROOF SURFACE: SYNTHETIC.

#include "pathobs/documents.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>

using namespace pathobs;

namespace {

Result<std::unique_ptr<ObservatoryRuntime>> make(const std::filesystem::path& store, TimePoint now) {
  RuntimeConfig config;
  config.limits = default_limits();
  config.persist = true;
  config.store_path = store;
  auto clock = std::make_unique<ManualClock>(now);
  PATHOBS_TRY(created, ObservatoryRuntime::create(config, std::move(clock)));
  const Status started = created->start();
  if (!started.has_value()) {
    return started.error();
  }
  return std::move(created);
}

} // namespace

int main() {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 2;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "%s\n", describe(fabric.error()).c_str());
    return 1;
  }

  const std::filesystem::path store = std::filesystem::temp_directory_path() /
                                      "pathobs-example-store.pob";
  std::error_code ignored;
  std::filesystem::remove(store, ignored);

  const TimePoint first_now = fabric.value().routes.received;
  Result<std::unique_ptr<ObservatoryRuntime>> first = make(store, first_now);
  if (!first.has_value()) {
    std::fprintf(stderr, "%s\n", describe(first.error()).c_str());
    return 1;
  }
  for (const auto& descriptor : fabric.value().sources) {
    static_cast<void>(first.value()->register_source(descriptor));
  }
  static_cast<void>(first.value()->publish_topology(fabric.value().topology));
  static_cast<void>(first.value()->publish_routes(fabric.value().routes));
  for (const auto& evidence : fabric.value().evidence) {
    static_cast<void>(first.value()->ingest_evidence(evidence));
  }
  Result<std::vector<ComparisonResult>> before = first.value()->compare_all();
  if (!before.has_value()) {
    std::fprintf(stderr, "%s\n", describe(before.error()).c_str());
    return 1;
  }
  std::printf("first process: %zu comparison(s), first outcome %s\n", before.value().size(),
              before.value().empty() ? "none" : to_string(before.value().front().outcome));
  static_cast<void>(first.value()->stop());
  first.value().reset();

  // Restart an hour later. The evidence is restored with its original receive
  // time, so it must be re-evaluated as stale rather than silently becoming
  // fresh, and the expected state must be republished.
  const TimePoint second_now =
      TimePoint::from_unix_nanos(first_now.unix_nanos() + 3600LL * 1000000000LL);
  Result<std::unique_ptr<ObservatoryRuntime>> second = make(store, second_now);
  if (!second.has_value()) {
    std::fprintf(stderr, "%s\n", describe(second.error()).c_str());
    return 1;
  }
  const RuntimeStats stats = second.value()->stats();
  std::printf("restarted process: %zu evidence bucket(s), %llu restored record(s)\n",
              stats.evidence_buckets,
              static_cast<unsigned long long>(stats.metrics.store_records_recovered));
  for (const auto& descriptor : fabric.value().sources) {
    static_cast<void>(second.value()->register_source(descriptor));
  }
  static_cast<void>(second.value()->publish_topology(fabric.value().topology));
  static_cast<void>(second.value()->publish_routes(fabric.value().routes));
  Result<std::vector<ComparisonResult>> after = second.value()->compare_all();
  if (!after.has_value()) {
    std::fprintf(stderr, "%s\n", describe(after.error()).c_str());
    return 1;
  }
  std::printf("after restart: first outcome %s (freshness %s)\n",
              after.value().empty() ? "none" : to_string(after.value().front().outcome),
              after.value().empty() ? "unknown" : to_string(after.value().front().freshness));
  static_cast<void>(second.value()->stop());
  second.value().reset();
  std::filesystem::remove(store, ignored);
  return 0;
}
