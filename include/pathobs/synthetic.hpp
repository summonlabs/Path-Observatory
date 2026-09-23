// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// PROOF SURFACE: SYNTHETIC.
//
// This file generates laboratory fabrics. Every object it produces is labelled
// ProofSurface::Synthetic and every source it registers is a simulator or a
// test harness. It exists so that tests, benchmarks, examples and tooling share
// one deterministic generator instead of each inventing its own model, and so
// that the synthetic surface of the product is easy to find and to audit.
//
// Nothing here models a real switch, ASIC, RDMA transport, InfiniBand fabric or
// multi host deployment, and nothing here should be reported as if it did.

#ifndef PATHOBS_SYNTHETIC_HPP
#define PATHOBS_SYNTHETIC_HPP

#include "pathobs/expected.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/source.hpp"
#include "pathobs/topology.hpp"

#include <cstdint>
#include <vector>

namespace pathobs::synthetic {

struct LabOptions {
  std::uint64_t seed{0};
  /// Number of devices on the path, at least two.
  std::uint32_t hop_count{4};
  /// Members of the path group. More than one is legitimate multipath.
  std::uint32_t multipath_members{1};

  ProofSurface surface{ProofSurface::Synthetic};
  AuthorityClass observer_authority{AuthorityClass::Advisory};
  SourceKind observer_kind{SourceKind::Simulator};

  std::uint64_t planning_source_id{1};
  std::uint64_t observer_source_id{2};
  std::uint64_t epoch{1};
  std::uint64_t incarnation{3};
  std::uint64_t route_generation{7};
  std::uint64_t topology_generation{5};
  std::uint64_t revision{12};

  /// How many evidence records to emit. They differ by source sequence.
  std::uint32_t evidence_count{1};
  /// First source sequence number. Replay is produced by lowering it.
  std::uint64_t first_sequence{1};
  /// Age of the evidence relative to the fabric creation time.
  std::int64_t evidence_age_nanos{0};

  /// Report a device that the plan does not contain.
  bool diverge{false};
  /// Truncate the trace and declare it partial.
  bool partial{false};
  /// Omit link identity so the observatory must correlate it.
  bool omit_links{false};
  /// Claim an epoch that has been superseded.
  bool stale_epoch{false};
  /// Claim a revision that the authority has already moved past.
  bool stale_revision{false};
  /// Claim a route generation the authority does not know.
  bool unknown_generation{false};
  /// Produce evidence with no hops at all.
  bool empty_trace{false};
};

struct LabFabric {
  TopologyGeneration topology{};
  RouteGeneration routes{};
  std::vector<SourceDescriptor> sources{};
  std::vector<ObservedPathEvidence> evidence{};
  DeviceId divergent_device{};
  PathId primary_path{};
  PathGroupId group{};
};

/// Build the fabric. Deterministic in the seed and the options.
PATHOBS_API Result<LabFabric> build_lab_fabric(const LabOptions& options, const Limits& limits);

/// The device identity used by a divergent observation.
PATHOBS_API DeviceId divergent_device_id() noexcept;

} // namespace pathobs::synthetic

#endif // PATHOBS_SYNTHETIC_HPP
