// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Runtime counters. Every counter is a monotonic total, never a rate, and every
// snapshot is taken with relaxed atomics because the totals are the contract,
// not their ordering relative to one another.

#ifndef PATHOBS_METRICS_HPP
#define PATHOBS_METRICS_HPP

#include "pathobs/divergence.hpp"
#include "pathobs/export.hpp"

#include <atomic>
#include <cstdint>

namespace pathobs {

struct MetricsSnapshot {
  std::uint64_t evidence_offered{0};
  std::uint64_t evidence_accepted{0};
  std::uint64_t evidence_rejected{0};
  std::uint64_t evidence_unregistered_source{0};
  std::uint64_t evidence_replayed{0};
  std::uint64_t evidence_gap_events{0};
  std::uint64_t comparisons{0};
  std::uint64_t matches{0};
  std::uint64_t divergences{0};
  std::uint64_t indeterminate{0};
  std::uint64_t unsupported{0};
  std::uint64_t no_expected_state{0};
  std::uint64_t findings_recorded{0};
  std::uint64_t findings_resolved{0};
  std::uint64_t findings_evicted{0};
  std::uint64_t queue_submitted{0};
  std::uint64_t queue_rejected{0};
  std::uint64_t tasks_completed{0};
  std::uint64_t tasks_cancelled{0};
  std::uint64_t store_appends{0};
  std::uint64_t store_records_recovered{0};
  std::uint64_t store_corrupt_tail_truncations{0};
  std::uint64_t transport_frames_in{0};
  std::uint64_t transport_frames_out{0};
  std::uint64_t transport_bytes_in{0};
  std::uint64_t transport_bytes_out{0};
  std::uint64_t transport_protocol_violations{0};

  friend bool operator==(const MetricsSnapshot&, const MetricsSnapshot&) noexcept = default;
};

class PATHOBS_API Metrics {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  void add_evidence_offered(std::uint64_t value);
  void add_evidence_accepted(std::uint64_t value);
  void add_evidence_rejected(std::uint64_t value);
  void add_evidence_unregistered_source(std::uint64_t value);
  void add_evidence_replayed(std::uint64_t value);
  void add_evidence_gap_events(std::uint64_t value);
  void add_comparison(MatchOutcome outcome);
  void add_finding_recorded();
  void add_finding_resolved(std::uint64_t value);
  void add_finding_evicted(std::uint64_t value);
  void add_queue_submitted();
  void add_queue_rejected();
  void add_task_completed();
  void add_task_cancelled();
  void add_store_append();
  void add_store_records_recovered(std::uint64_t value);
  void add_store_corrupt_tail_truncation();
  void add_transport_frame_in(std::uint64_t bytes);
  void add_transport_frame_out(std::uint64_t bytes);
  void add_transport_protocol_violation();

  MetricsSnapshot snapshot() const;
  void reset();

 private:
  std::atomic<std::uint64_t> evidence_offered_{0};
  std::atomic<std::uint64_t> evidence_accepted_{0};
  std::atomic<std::uint64_t> evidence_rejected_{0};
  std::atomic<std::uint64_t> evidence_unregistered_source_{0};
  std::atomic<std::uint64_t> evidence_replayed_{0};
  std::atomic<std::uint64_t> evidence_gap_events_{0};
  std::atomic<std::uint64_t> comparisons_{0};
  std::atomic<std::uint64_t> matches_{0};
  std::atomic<std::uint64_t> divergences_{0};
  std::atomic<std::uint64_t> indeterminate_{0};
  std::atomic<std::uint64_t> unsupported_{0};
  std::atomic<std::uint64_t> no_expected_state_{0};
  std::atomic<std::uint64_t> findings_recorded_{0};
  std::atomic<std::uint64_t> findings_resolved_{0};
  std::atomic<std::uint64_t> findings_evicted_{0};
  std::atomic<std::uint64_t> queue_submitted_{0};
  std::atomic<std::uint64_t> queue_rejected_{0};
  std::atomic<std::uint64_t> tasks_completed_{0};
  std::atomic<std::uint64_t> tasks_cancelled_{0};
  std::atomic<std::uint64_t> store_appends_{0};
  std::atomic<std::uint64_t> store_records_recovered_{0};
  std::atomic<std::uint64_t> store_corrupt_tail_truncations_{0};
  std::atomic<std::uint64_t> transport_frames_in_{0};
  std::atomic<std::uint64_t> transport_frames_out_{0};
  std::atomic<std::uint64_t> transport_bytes_in_{0};
  std::atomic<std::uint64_t> transport_bytes_out_{0};
  std::atomic<std::uint64_t> transport_protocol_violations_{0};
};

} // namespace pathobs

#endif // PATHOBS_METRICS_HPP
