// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/history.hpp"

#include <algorithm>

namespace pathobs {
namespace {

bool same_subject(const HistoryRecord& record, MaybeId<PathGroupId> group, MaybeId<PathId> path) {
  if (group.present() && record.group.present() && group.value() == record.group.value()) {
    return true;
  }
  if (path.present() && record.path.present() && path.value() == record.path.value()) {
    return true;
  }
  return false;
}

} // namespace

HistoryStore::HistoryStore(Limits limits) : limits_(limits) {}

Status HistoryStore::append(const ComparisonResult& result) {
  HistoryRecord record;
  record.id = result.id;
  record.generation = result.generation;
  record.topology = result.topology;
  record.group = result.group;
  record.path = result.path;
  record.matched_path = result.matched_path;
  record.outcome = result.outcome;
  record.primary = result.primary;
  record.freshness = result.freshness;
  record.completeness = result.completeness;
  record.consistency = result.consistency;
  record.surface = result.surface;
  record.evidence_considered = static_cast<std::uint32_t>(result.evidence_considered);
  record.evidence_rejected = static_cast<std::uint32_t>(result.evidence_rejected);
  record.evaluated_at = result.evaluated_at;

  std::lock_guard<std::mutex> lock(mutex_);
  records_.push_back(std::move(record));

  // Per subject bound: evict the oldest record of any subject that is over its
  // own cap, oldest first, deterministically.
  for (;;) {
    std::size_t worst_count = 0;
    std::size_t worst_index = records_.size();
    for (std::size_t i = 0; i < records_.size(); ++i) {
      std::size_t count = 0;
      for (std::size_t j = 0; j < records_.size(); ++j) {
        if (same_subject(records_[j], records_[i].group, records_[i].path)) {
          ++count;
        }
      }
      if (count > limits_.max_history_per_group && count > worst_count) {
        worst_count = count;
        worst_index = i;
      }
    }
    if (worst_index == records_.size()) {
      break;
    }
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(worst_index));
    ++evicted_by_limit_;
  }

  while (records_.size() > limits_.max_history_records) {
    records_.pop_front();
    ++evicted_by_limit_;
  }
  return ok();
}

HistoryQueryResult HistoryStore::query(const HistoryQuery& request) const {
  HistoryQueryResult result;
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<const HistoryRecord*> selected;
  for (const auto& record : records_) {
    if (request.group.present() || request.path.present()) {
      if (!same_subject(record, request.group, request.path)) {
        ++result.excluded_by_filter;
        continue;
      }
    }
    if (request.generation.present() && record.generation != request.generation.value()) {
      ++result.excluded_by_filter;
      continue;
    }
    if (request.since.is_set() && record.evaluated_at.is_set() &&
        record.evaluated_at < request.since) {
      ++result.excluded_by_filter;
      continue;
    }
    if (request.filter_by_outcome && record.outcome != request.outcome_filter) {
      ++result.excluded_by_filter;
      continue;
    }
    if (request.apply_window && request.now.is_set() && record.evaluated_at.is_set()) {
      const std::int64_t age = request.now.unix_nanos() - record.evaluated_at.unix_nanos();
      if (age > 0 && static_cast<std::uint64_t>(age) > limits_.history_window_nanos) {
        ++result.excluded_by_window;
        continue;
      }
    }
    selected.push_back(&record);
  }

  result.matched = selected.size();
  // Newest first; ties broken by identity so the order never depends on
  // insertion order alone.
  std::sort(selected.begin(), selected.end(),
            [](const HistoryRecord* a, const HistoryRecord* b) {
              if (!(a->evaluated_at == b->evaluated_at)) {
                return a->evaluated_at > b->evaluated_at;
              }
              return a->id > b->id;
            });

  const std::size_t limit = std::min<std::size_t>(request.limit, limits_.max_export_records);
  for (const HistoryRecord* record : selected) {
    if (result.records.size() >= limit) {
      result.truncated = true;
      break;
    }
    result.records.push_back(*record);
  }
  return result;
}

Result<std::size_t> HistoryStore::prune(TimePoint now) {
  if (!now.is_set()) {
    return Error{ErrorCode::InvalidArgument, "history prune requires an evaluation time"};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t removed = 0;
  while (!records_.empty()) {
    const HistoryRecord& oldest = records_.front();
    if (!oldest.evaluated_at.is_set()) {
      break;
    }
    const std::int64_t age = now.unix_nanos() - oldest.evaluated_at.unix_nanos();
    if (age <= 0 || static_cast<std::uint64_t>(age) <= limits_.history_window_nanos) {
      break;
    }
    records_.pop_front();
    ++removed;
    ++evicted_by_window_;
  }
  return removed;
}

std::size_t HistoryStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

std::uint64_t HistoryStore::evicted_by_limit() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evicted_by_limit_;
}

std::uint64_t HistoryStore::evicted_by_window() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evicted_by_window_;
}

void HistoryStore::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
}

} // namespace pathobs
