// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A downstream consumer. It uses only the installed public API and proves that
// an installed package is usable without the Path Observatory source tree.
//
// PROOF SURFACE: SYNTHETIC.

#include <pathobs/compare.hpp>
#include <pathobs/documents.hpp>
#include <pathobs/runtime.hpp>
#include <pathobs/synthetic.hpp>
#include <pathobs/version.hpp>

#include <cstdio>
#include <memory>

int main() {
  using namespace pathobs;

  if (version().major != 1) {
    std::fprintf(stderr, "unexpected library version %s\n", version_string());
    return 1;
  }

  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 2;

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
  if (!runtime->start().has_value()) {
    return 1;
  }
  for (const auto& descriptor : fabric.value().sources) {
    static_cast<void>(runtime->register_source(descriptor));
  }
  static_cast<void>(runtime->publish_topology(fabric.value().topology));
  static_cast<void>(runtime->publish_routes(fabric.value().routes));
  for (const auto& evidence : fabric.value().evidence) {
    static_cast<void>(runtime->ingest_evidence(evidence));
  }

  Result<std::vector<ComparisonResult>> results = runtime->compare_all();
  if (!results.has_value() || results.value().empty()) {
    std::fprintf(stderr, "downstream consumer produced no comparison\n");
    return 1;
  }
  const ComparisonResult& result = results.value().front();
  if (!result.is_compliance()) {
    std::fprintf(stderr, "downstream consumer expected compliance, saw %s\n",
                 to_string(result.outcome));
    return 1;
  }

  std::printf("downstream consumer: %s on %s evidence, %zu hop difference(s)\n",
              to_string(result.outcome), to_string(result.surface), result.differences.size());
  static_cast<void>(runtime->stop());
  return 0;
}
