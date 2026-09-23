// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Bounded comparison history.
//
// History is bounded three ways at once: a global record cap, a per subject
// cap, and an aggregation window. The window is evaluated against a supplied
// time, never against a wall clock, so replaying a history produces the same
// answer whenever it is replayed. Every eviction is counted: a query result is
// never silently incomplete.

#ifndef PATHOBS_HISTORY_HPP
#define PATHOBS_HISTORY_HPP

#include "pathobs/compare.hpp"
#include "pathobs/divergence.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace pathobs {

struct HistoryRecord {
  ComparisonId id{};
  RouteGenerationId generation{};
  TopologyGenerationId topology{};
  MaybeId<PathGroupId> group{};
  MaybeId<PathId> path{};
  MaybeId<PathId> matched_path{};
  MatchOutcome outcome{MatchOutcome::NoExpectedState};
  DivergenceClass primary{DivergenceClass::None};
  Freshness freshness{Freshness::Unknown};
  EvidenceCompleteness completeness{EvidenceCompleteness::Unknown};
  ObservationConsistency consistency{ObservationConsistency::Unknown};
  ProofSurface surface{ProofSurface::Unknown};
  std::uint32_t evidence_considered{0};
  std::uint32_t evidence_rejected{0};
  TimePoint evaluated_at{};

  friend bool operator==(const HistoryRecord&, const HistoryRecord&) noexcept = default;
};

struct HistoryQuery {
  MaybeId<PathGroupId> group{};
  MaybeId<PathId> path{};
  MaybeId<RouteGenerationId> generation{};
  /// Only records at or after this instant. Unset means no lower bound.
  TimePoint since{};
  MatchOutcome outcome_filter{MatchOutcome::NoExpectedState};
  bool filter_by_outcome{false};
  std::size_t limit{256};
  /// When set, records older than the aggregation window relative to this
  /// instant are excluded from the answer and counted as out of window.
  TimePoint now{};
  bool apply_window{false};
};

struct HistoryQueryResult {
  std::vector<HistoryRecord> records{};
  std::uint64_t matched{0};
  std::uint64_t excluded_by_window{0};
  std::uint64_t excluded_by_filter{0};
  /// True when the limit cut the answer short. Reported, never hidden.
  bool truncated{false};
};

class PATHOBS_API HistoryStore {
 public:
  explicit HistoryStore(Limits limits);

  Status append(const ComparisonResult& result);

  HistoryQueryResult query(const HistoryQuery& request) const;

  /// Drop every record older than the aggregation window relative to now.
  Result<std::size_t> prune(TimePoint now);

  std::size_t size() const;
  std::uint64_t evicted_by_limit() const;
  std::uint64_t evicted_by_window() const;
  void clear();

 private:
  mutable std::mutex mutex_;
  Limits limits_;
  std::deque<HistoryRecord> records_;
  std::uint64_t evicted_by_limit_{0};
  std::uint64_t evicted_by_window_{0};
};

} // namespace pathobs

#endif // PATHOBS_HISTORY_HPP
