// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Benchmark: ingest, compare and persist a bounded workload.
//
// PROOF SURFACE: SYNTHETIC.

#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace pathobs;

int main(int argc, char** argv) {
  std::uint64_t rounds = 200;
  if (argc > 1) {
    rounds = std::strtoull(argv[1], nullptr, 10);
  }
  if (rounds == 0) {
    rounds = 1;
  }

  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 8;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "%s\n", describe(fabric.error()).c_str());
    return 1;
  }

  const std::filesystem::path store =
      std::filesystem::temp_directory_path() / "pathobs-bench-store.pob";
  std::error_code ignored;
  std::filesystem::remove(store, ignored);

  RuntimeConfig config;
  config.limits = limits;
  config.persist = true;
  config.store_path = store;
  config.worker_threads = 2;
  auto clock = std::make_unique<ManualClock>(fabric.value().routes.received);
  Result<std::unique_ptr<ObservatoryRuntime>> created =
      ObservatoryRuntime::create(config, std::move(clock));
  if (!created.has_value()) {
    std::fprintf(stderr, "%s\n", describe(created.error()).c_str());
    return 1;
  }
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  static_cast<void>(runtime->start());
  for (const auto& descriptor : fabric.value().sources) {
    static_cast<void>(runtime->register_source(descriptor));
  }
  static_cast<void>(runtime->publish_topology(fabric.value().topology));
  static_cast<void>(runtime->publish_routes(fabric.value().routes));

  std::uint64_t completed_rounds = 0;
  std::uint64_t accepted = 0;
  std::uint64_t comparisons = 0;
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t round = 0; round < rounds; ++round) {
    const Status submitted = runtime->submit_batch(fabric.value().evidence);
    if (!submitted.has_value()) {
      break;
    }
    const Status drained = runtime->drain();
    if (!drained.has_value()) {
      break;
    }
    ++completed_rounds;
  }
  const auto finished = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(finished - started).count();

  const RuntimeStats stats = runtime->stats();
  accepted = stats.metrics.evidence_accepted;
  comparisons = stats.history;
  static_cast<void>(runtime->stop());
  runtime.reset();
  std::filesystem::remove(store, ignored);

  std::printf("bench_ingest\n");
  std::printf("  rounds requested   : %llu\n", static_cast<unsigned long long>(rounds));
  std::printf("  rounds completed   : %llu\n", static_cast<unsigned long long>(completed_rounds));
  std::printf("  evidence accepted  : %llu\n", static_cast<unsigned long long>(accepted));
  std::printf("  comparisons stored : %llu\n", static_cast<unsigned long long>(comparisons));
  std::printf("  elapsed seconds    : %.6f\n", seconds);
  std::printf("  rounds per second  : %.0f\n",
              seconds > 0.0 ? static_cast<double>(completed_rounds) / seconds : 0.0);
  return completed_rounds == rounds ? 0 : 1;
}
