// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Bounds.
//
// Every unbounded quantity in the runtime has a configured limit and a hard
// ceiling that configuration may not exceed. Limits cover workers, queues,
// payloads, metadata, history, result sets, persistence growth and aggregation
// windows. A limit breach is an error with a stable error code, never a silent
// truncation and never an unbounded allocation.

#ifndef PATHOBS_LIMITS_HPP
#define PATHOBS_LIMITS_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <cstdint>

namespace pathobs {

struct Limits {
  // --- topology and expected path shape -----------------------------------
  std::uint32_t max_hops_per_path = 512;
  std::uint32_t max_paths_per_generation = 65536;
  std::uint32_t max_groups_per_generation = 65536;
  std::uint32_t max_members_per_group = 64;
  std::uint32_t max_devices_per_topology = 262144;
  std::uint32_t max_links_per_topology = 1048576;

  // --- evidence ------------------------------------------------------------
  std::uint32_t max_evidence_per_comparison = 4096;
  std::uint32_t max_evidence_per_ingest_batch = 8192;
  std::uint64_t max_evidence_payload_bytes = 4ull * 1024 * 1024;

  // --- text / structured input --------------------------------------------
  std::uint32_t max_document_bytes = 64u * 1024 * 1024;
  std::uint32_t max_json_depth = 64;
  std::uint32_t max_json_nodes = 2000000;
  std::uint32_t max_string_bytes = 1024 * 1024;

  // --- runtime -------------------------------------------------------------
  std::uint32_t worker_threads = 4;
  std::uint32_t max_worker_threads = 64;
  std::uint32_t max_queue_depth = 16384;
  std::uint32_t max_transport_connections = 64;
  std::uint32_t max_frame_bytes = 8u * 1024 * 1024;
  std::uint32_t max_connections_per_accept = 256;

  // --- results and history -------------------------------------------------
  std::uint32_t max_history_records = 65536;
  std::uint32_t max_history_per_group = 1024;
  std::uint64_t history_window_nanos = 3600ull * 1000000000ull;
  std::uint32_t max_findings = 65536;
  std::uint32_t max_finding_evidence = 512;
  std::uint32_t max_export_records = 1000000;

  // --- persistence ---------------------------------------------------------
  std::uint64_t max_store_bytes = 512ull * 1024 * 1024;
  std::uint32_t max_store_records = 1000000;
  std::uint32_t max_store_record_bytes = 16u * 1024 * 1024;
  std::uint32_t max_store_segments = 8;

  /// Reject a configuration that exceeds any hard ceiling or is internally
  /// inconsistent (for example a per-group history cap above the global cap).
  Result<void> validate() const;

  /// The largest configuration the runtime will ever accept.
  static Limits hard_ceiling() noexcept;
};

PATHOBS_API const Limits& default_limits() noexcept;

} // namespace pathobs

#endif // PATHOBS_LIMITS_HPP
