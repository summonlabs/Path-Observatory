// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Compare one observed trace against a declared plan.
//
// PROOF SURFACE: SYNTHETIC. The fabric comes from the laboratory generator; the
// output says so.

#include "pathobs/documents.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"

#include <cstdio>
#include <iostream>
#include <memory>

using namespace pathobs;

int main() {
  const Limits limits = default_limits();

  synthetic::LabOptions lab;
  lab.evidence_count = 1;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "%s\n", describe(fabric.error()).c_str());
    return 1;
  }

  RuntimeConfig config;
  config.limits = limits;
  auto clock = std::make_unique<ManualClock>(fabric.value().routes.received);
  Result<std::unique_ptr<ObservatoryRuntime>> created =
      ObservatoryRuntime::create(config, std::move(clock));
  if (!created.has_value()) {
    std::fprintf(stderr, "%s\n", describe(created.error()).c_str());
    return 1;
  }
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  const Status started = runtime->start();
  if (!started.has_value()) {
    std::fprintf(stderr, "%s\n", describe(started.error()).c_str());
    return 1;
  }

  for (const auto& descriptor : fabric.value().sources) {
    const Status registered = runtime->register_source(descriptor);
    if (!registered.has_value()) {
      std::fprintf(stderr, "%s\n", describe(registered.error()).c_str());
      return 1;
    }
  }
  if (!runtime->publish_topology(fabric.value().topology).has_value()) {
    std::fprintf(stderr, "topology publication failed\n");
    return 1;
  }
  if (!runtime->publish_routes(fabric.value().routes).has_value()) {
    std::fprintf(stderr, "route publication failed\n");
    return 1;
  }
  for (const auto& evidence : fabric.value().evidence) {
    const Result<IngestResult> ingested = runtime->ingest_evidence(evidence);
    if (!ingested.has_value() || !ingested.value().accepted) {
      std::fprintf(stderr, "evidence was not accepted\n");
      return 1;
    }
  }

  Result<std::vector<ComparisonResult>> results = runtime->compare_all();
  if (!results.has_value()) {
    std::fprintf(stderr, "%s\n", describe(results.error()).c_str());
    return 1;
  }
  for (const auto& result : results.value()) {
    std::cout << documents::to_json(result, true) << std::endl;
  }
  static_cast<void>(runtime->stop());
  return 0;
}
