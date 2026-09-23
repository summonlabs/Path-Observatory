// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/limits.hpp"

#include <string>

namespace pathobs {
namespace {

/// Compare one configured field against its hard ceiling.
Status within(std::uint64_t value, std::uint64_t ceiling, const char* name) {
  if (value > ceiling) {
    return Error{ErrorCode::OutOfRange, std::string("limit exceeds hard ceiling: ") + name};
  }
  return ok();
}

} // namespace

Limits Limits::hard_ceiling() noexcept {
  Limits limits;
  limits.max_hops_per_path = 4096;
  limits.max_paths_per_generation = 1000000;
  limits.max_groups_per_generation = 1000000;
  limits.max_members_per_group = 4096;
  limits.max_devices_per_topology = 1000000;
  limits.max_links_per_topology = 4000000;
  limits.max_evidence_per_comparison = 65536;
  limits.max_evidence_per_ingest_batch = 65536;
  limits.max_evidence_payload_bytes = 64ull * 1024 * 1024;
  limits.max_document_bytes = 512u * 1024 * 1024;
  limits.max_json_depth = 256;
  limits.max_json_nodes = 20000000;
  limits.max_string_bytes = 16u * 1024 * 1024;
  limits.worker_threads = 256;
  limits.max_worker_threads = 256;
  limits.max_queue_depth = 1048576;
  limits.max_transport_connections = 1024;
  limits.max_frame_bytes = 128u * 1024 * 1024;
  limits.max_connections_per_accept = 65536;
  limits.max_history_records = 10000000;
  limits.max_history_per_group = 1000000;
  limits.history_window_nanos = 365ull * 24ull * 3600ull * 1000000000ull;
  limits.max_findings = 10000000;
  limits.max_finding_evidence = 65536;
  limits.max_export_records = 10000000;
  limits.max_store_bytes = 64ull * 1024 * 1024 * 1024;
  limits.max_store_records = 100000000;
  limits.max_store_record_bytes = 256u * 1024 * 1024;
  limits.max_store_segments = 64;
  return limits;
}

Result<void> Limits::validate() const {
  const Limits ceiling = hard_ceiling();
  const Status checks[] = {
      within(max_hops_per_path, ceiling.max_hops_per_path, "max_hops_per_path"),
      within(max_paths_per_generation, ceiling.max_paths_per_generation, "max_paths_per_generation"),
      within(max_groups_per_generation, ceiling.max_groups_per_generation,
             "max_groups_per_generation"),
      within(max_members_per_group, ceiling.max_members_per_group, "max_members_per_group"),
      within(max_devices_per_topology, ceiling.max_devices_per_topology,
             "max_devices_per_topology"),
      within(max_links_per_topology, ceiling.max_links_per_topology, "max_links_per_topology"),
      within(max_evidence_per_comparison, ceiling.max_evidence_per_comparison,
             "max_evidence_per_comparison"),
      within(max_evidence_per_ingest_batch, ceiling.max_evidence_per_ingest_batch,
             "max_evidence_per_ingest_batch"),
      within(max_evidence_payload_bytes, ceiling.max_evidence_payload_bytes,
             "max_evidence_payload_bytes"),
      within(max_document_bytes, ceiling.max_document_bytes, "max_document_bytes"),
      within(max_json_depth, ceiling.max_json_depth, "max_json_depth"),
      within(max_json_nodes, ceiling.max_json_nodes, "max_json_nodes"),
      within(max_string_bytes, ceiling.max_string_bytes, "max_string_bytes"),
      within(worker_threads, ceiling.worker_threads, "worker_threads"),
      within(max_worker_threads, ceiling.max_worker_threads, "max_worker_threads"),
      within(max_queue_depth, ceiling.max_queue_depth, "max_queue_depth"),
      within(max_transport_connections, ceiling.max_transport_connections,
             "max_transport_connections"),
      within(max_frame_bytes, ceiling.max_frame_bytes, "max_frame_bytes"),
      within(max_connections_per_accept, ceiling.max_connections_per_accept,
             "max_connections_per_accept"),
      within(max_history_records, ceiling.max_history_records, "max_history_records"),
      within(max_history_per_group, ceiling.max_history_per_group, "max_history_per_group"),
      within(max_findings, ceiling.max_findings, "max_findings"),
      within(max_finding_evidence, ceiling.max_finding_evidence, "max_finding_evidence"),
      within(max_export_records, ceiling.max_export_records, "max_export_records"),
      within(max_store_bytes, ceiling.max_store_bytes, "max_store_bytes"),
      within(max_store_records, ceiling.max_store_records, "max_store_records"),
      within(max_store_record_bytes, ceiling.max_store_record_bytes, "max_store_record_bytes"),
      within(max_store_segments, ceiling.max_store_segments, "max_store_segments"),
  };
  for (const Status& status : checks) {
    if (!status.has_value()) {
      return status.error();
    }
  }

  if (worker_threads == 0) {
    return Error{ErrorCode::InvalidArgument, "worker_threads must be at least one"};
  }
  if (max_history_per_group > max_history_records) {
    return Error{ErrorCode::InvalidArgument,
                 "max_history_per_group must not exceed max_history_records"};
  }
  if (max_evidence_per_ingest_batch > max_queue_depth) {
    return Error{ErrorCode::InvalidArgument,
                 "max_evidence_per_ingest_batch must not exceed max_queue_depth"};
  }
  return ok();
}

const Limits& default_limits() noexcept {
  static const Limits limits{};
  return limits;
}

} // namespace pathobs
