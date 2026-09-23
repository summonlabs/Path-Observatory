// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Feed evidence through the bounded worker pipeline.
//
// PROOF SURFACE: SYNTHETIC.

#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"

#include <cstdio>
#include <memory>

using namespace pathobs;

int main() {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 8;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "%s\n", describe(fabric.error()).c_str());
    return 1;
  }

  RuntimeConfig config;
  config.limits = limits;
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

  // Split the evidence across four submitted batches and let the pool process
  // them; drain() returns only when no batch is queued or running.
  const std::size_t batch_size = (fabric.value().evidence.size() + 3) / 4;
  std::size_t submitted = 0;
  for (std::size_t start = 0; start < fabric.value().evidence.size(); start += batch_size) {
    const std::size_t end = start + batch_size > fabric.value().evidence.size()
                                ? fabric.value().evidence.size()
                                : start + batch_size;
    std::vector<ObservedPathEvidence> batch(fabric.value().evidence.begin() +
                                                static_cast<std::ptrdiff_t>(start),
                                            fabric.value().evidence.begin() +
                                                static_cast<std::ptrdiff_t>(end));
    const Status queued = runtime->submit_batch(std::move(batch));
    if (!queued.has_value()) {
      std::fprintf(stderr, "%s\n", describe(queued.error()).c_str());
      return 1;
    }
    ++submitted;
  }
  const Status drained = runtime->drain();
  if (!drained.has_value()) {
    std::fprintf(stderr, "%s\n", describe(drained.error()).c_str());
    return 1;
  }
  const RuntimeStats stats = runtime->stats();
  std::printf("submitted %zu batch(es); accepted %llu evidence record(s); %zu comparison(s)\n",
              submitted, static_cast<unsigned long long>(stats.metrics.evidence_accepted),
              stats.history);
  static_cast<void>(runtime->stop());
  return 0;
}
