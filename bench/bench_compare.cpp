// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Benchmark: end to end comparison throughput.
//
// The benchmark measures completed work only: it counts a comparison as done
// when compare() has returned a sealed result, and it prints the completed
// count alongside the elapsed time so a truncated run cannot be mistaken for a
// fast one.
//
// PROOF SURFACE: SYNTHETIC.

#include "pathobs/compare.hpp"
#include "pathobs/synthetic.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace pathobs;

int main(int argc, char** argv) {
  std::uint64_t iterations = 20000;
  if (argc > 1) {
    iterations = std::strtoull(argv[1], nullptr, 10);
  }
  if (iterations == 0) {
    iterations = 1;
  }

  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 4;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "%s\n", describe(fabric.error()).c_str());
    return 1;
  }

  Result<TopologyIndex> built = TopologyIndex::build(fabric.value().topology, limits);
  if (!built.has_value()) {
    std::fprintf(stderr, "%s\n", describe(built.error()).c_str());
    return 1;
  }
  TopologyIndex& index = built.value();

  ComparisonContext context;
  context.routes = &fabric.value().routes;
  context.topology = &fabric.value().topology;
  context.topology_index = &index;
  context.authority.epoch = fabric.value().routes.epoch;
  context.authority.incarnation = fabric.value().routes.incarnation;
  context.authority.route_revision = fabric.value().routes.revision;
  context.authority.topology_revision = fabric.value().topology.revision;
  context.now = fabric.value().routes.received;

  ComparisonRequest request;
  request.generation = fabric.value().routes.id;
  request.group = MaybeId<PathGroupId>(fabric.value().group);
  request.evidence = fabric.value().evidence;

  const ComparisonEngine engine(limits, ComparisonPolicy{});

  std::uint64_t completed = 0;
  std::uint64_t matched = 0;
  std::uint64_t digest_accumulator = 0;
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < iterations; ++i) {
    Result<ComparisonResult> result = engine.compare(request, context);
    if (!result.has_value()) {
      std::fprintf(stderr, "%s\n", describe(result.error()).c_str());
      return 1;
    }
    if (result.value().outcome == MatchOutcome::Match) {
      ++matched;
    }
    digest_accumulator ^= result.value().canonical_digest().bytes()[0];
    ++completed;
  }
  const auto finished = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(finished - started).count();

  std::printf("bench_compare\n");
  std::printf("  iterations requested : %llu\n", static_cast<unsigned long long>(iterations));
  std::printf("  comparisons completed: %llu\n", static_cast<unsigned long long>(completed));
  std::printf("  matches              : %llu\n", static_cast<unsigned long long>(matched));
  std::printf("  digest accumulator   : %llu\n", static_cast<unsigned long long>(digest_accumulator));
  std::printf("  elapsed seconds      : %.6f\n", seconds);
  std::printf("  comparisons per second: %.0f\n",
              seconds > 0.0 ? static_cast<double>(completed) / seconds : 0.0);
  return completed == iterations ? 0 : 1;
}
