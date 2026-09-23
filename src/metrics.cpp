// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/metrics.hpp"

#include "pathobs/divergence.hpp"

namespace pathobs {

void Metrics::add_evidence_offered(std::uint64_t value) {
  evidence_offered_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_evidence_accepted(std::uint64_t value) {
  evidence_accepted_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_evidence_rejected(std::uint64_t value) {
  evidence_rejected_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_evidence_unregistered_source(std::uint64_t value) {
  evidence_unregistered_source_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_evidence_replayed(std::uint64_t value) {
  evidence_replayed_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_evidence_gap_events(std::uint64_t value) {
  evidence_gap_events_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_comparison(MatchOutcome outcome) {
  comparisons_.fetch_add(1, std::memory_order_relaxed);
  switch (outcome) {
    case MatchOutcome::Match:
      matches_.fetch_add(1, std::memory_order_relaxed);
      break;
    case MatchOutcome::Divergent:
      divergences_.fetch_add(1, std::memory_order_relaxed);
      break;
    case MatchOutcome::Indeterminate:
      indeterminate_.fetch_add(1, std::memory_order_relaxed);
      break;
    case MatchOutcome::Unsupported:
      unsupported_.fetch_add(1, std::memory_order_relaxed);
      break;
    case MatchOutcome::NoExpectedState:
      no_expected_state_.fetch_add(1, std::memory_order_relaxed);
      break;
  }
}

void Metrics::add_finding_recorded() { findings_recorded_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_finding_resolved(std::uint64_t value) {
  findings_resolved_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_finding_evicted(std::uint64_t value) {
  findings_evicted_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_queue_submitted() { queue_submitted_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_queue_rejected() { queue_rejected_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_task_completed() { tasks_completed_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_task_cancelled() { tasks_cancelled_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_store_append() { store_appends_.fetch_add(1, std::memory_order_relaxed); }

void Metrics::add_store_records_recovered(std::uint64_t value) {
  store_records_recovered_.fetch_add(value, std::memory_order_relaxed);
}

void Metrics::add_store_corrupt_tail_truncation() {
  store_corrupt_tail_truncations_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::add_transport_frame_in(std::uint64_t bytes) {
  transport_frames_in_.fetch_add(1, std::memory_order_relaxed);
  transport_bytes_in_.fetch_add(bytes, std::memory_order_relaxed);
}

void Metrics::add_transport_frame_out(std::uint64_t bytes) {
  transport_frames_out_.fetch_add(1, std::memory_order_relaxed);
  transport_bytes_out_.fetch_add(bytes, std::memory_order_relaxed);
}

void Metrics::add_transport_protocol_violation() {
  transport_protocol_violations_.fetch_add(1, std::memory_order_relaxed);
}

MetricsSnapshot Metrics::snapshot() const {
  MetricsSnapshot out;
  out.evidence_offered = evidence_offered_.load(std::memory_order_relaxed);
  out.evidence_accepted = evidence_accepted_.load(std::memory_order_relaxed);
  out.evidence_rejected = evidence_rejected_.load(std::memory_order_relaxed);
  out.evidence_unregistered_source = evidence_unregistered_source_.load(std::memory_order_relaxed);
  out.evidence_replayed = evidence_replayed_.load(std::memory_order_relaxed);
  out.evidence_gap_events = evidence_gap_events_.load(std::memory_order_relaxed);
  out.comparisons = comparisons_.load(std::memory_order_relaxed);
  out.matches = matches_.load(std::memory_order_relaxed);
  out.divergences = divergences_.load(std::memory_order_relaxed);
  out.indeterminate = indeterminate_.load(std::memory_order_relaxed);
  out.unsupported = unsupported_.load(std::memory_order_relaxed);
  out.no_expected_state = no_expected_state_.load(std::memory_order_relaxed);
  out.findings_recorded = findings_recorded_.load(std::memory_order_relaxed);
  out.findings_resolved = findings_resolved_.load(std::memory_order_relaxed);
  out.findings_evicted = findings_evicted_.load(std::memory_order_relaxed);
  out.queue_submitted = queue_submitted_.load(std::memory_order_relaxed);
  out.queue_rejected = queue_rejected_.load(std::memory_order_relaxed);
  out.tasks_completed = tasks_completed_.load(std::memory_order_relaxed);
  out.tasks_cancelled = tasks_cancelled_.load(std::memory_order_relaxed);
  out.store_appends = store_appends_.load(std::memory_order_relaxed);
  out.store_records_recovered = store_records_recovered_.load(std::memory_order_relaxed);
  out.store_corrupt_tail_truncations = store_corrupt_tail_truncations_.load(std::memory_order_relaxed);
  out.transport_frames_in = transport_frames_in_.load(std::memory_order_relaxed);
  out.transport_frames_out = transport_frames_out_.load(std::memory_order_relaxed);
  out.transport_bytes_in = transport_bytes_in_.load(std::memory_order_relaxed);
  out.transport_bytes_out = transport_bytes_out_.load(std::memory_order_relaxed);
  out.transport_protocol_violations = transport_protocol_violations_.load(std::memory_order_relaxed);
  return out;
}

void Metrics::reset() {
  evidence_offered_ = 0;
  evidence_accepted_ = 0;
  evidence_rejected_ = 0;
  evidence_unregistered_source_ = 0;
  evidence_replayed_ = 0;
  evidence_gap_events_ = 0;
  comparisons_ = 0;
  matches_ = 0;
  divergences_ = 0;
  indeterminate_ = 0;
  unsupported_ = 0;
  no_expected_state_ = 0;
  findings_recorded_ = 0;
  findings_resolved_ = 0;
  findings_evicted_ = 0;
  queue_submitted_ = 0;
  queue_rejected_ = 0;
  tasks_completed_ = 0;
  tasks_cancelled_ = 0;
  store_appends_ = 0;
  store_records_recovered_ = 0;
  store_corrupt_tail_truncations_ = 0;
  transport_frames_in_ = 0;
  transport_frames_out_ = 0;
  transport_bytes_in_ = 0;
  transport_bytes_out_ = 0;
  transport_protocol_violations_ = 0;
}

} // namespace pathobs
