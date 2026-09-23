// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/finding.hpp"

#include <algorithm>

namespace pathobs {
namespace {

/// Keep whichever record has seen the finding more recently.
bool finder_keeps_newest(const Finding& existing, const Finding& candidate) {
  if (candidate.last_seen != existing.last_seen) {
    return candidate.last_seen > existing.last_seen;
  }
  return candidate.occurrences > existing.occurrences;
}

} // namespace

const char* to_string(FindingStatus status) noexcept {
  switch (status) {
    case FindingStatus::Open:
      return "open";
    case FindingStatus::Acknowledged:
      return "acknowledged";
    case FindingStatus::Resolved:
      return "resolved";
  }
  return "open";
}

Result<FindingStatus> parse_finding_status(std::string_view text) {
  if (text == "open") return FindingStatus::Open;
  if (text == "acknowledged") return FindingStatus::Acknowledged;
  if (text == "resolved") return FindingStatus::Resolved;
  return Error{ErrorCode::InvalidFormat, "unknown finding status", std::string(text)};
}

Digest Finding::identity_digest() const {
  CanonicalWriter writer;
  writer.text("pathobs.finding");
  writer.u8(static_cast<std::uint8_t>(cls));
  writer.boolean(group.present());
  writer.id(group.value());
  writer.boolean(path.present());
  writer.id(path.value());
  writer.id(generation);
  writer.id(topology);
  std::vector<EvidenceId> sorted_evidence = evidence;
  std::sort(sorted_evidence.begin(), sorted_evidence.end());
  sorted_evidence.erase(std::unique(sorted_evidence.begin(), sorted_evidence.end()),
                        sorted_evidence.end());
  writer.count(sorted_evidence.size());
  for (const auto& evidence_id : sorted_evidence) {
    writer.digest_id(evidence_id);
  }
  std::vector<std::uint32_t> sorted_hops = hop_indices;
  std::sort(sorted_hops.begin(), sorted_hops.end());
  sorted_hops.erase(std::unique(sorted_hops.begin(), sorted_hops.end()), sorted_hops.end());
  writer.count(sorted_hops.size());
  for (const auto hop : sorted_hops) {
    writer.u32(hop);
  }
  return writer.digest();
}

void Finding::seal() { id = FindingId::from_digest(identity_digest()); }

FindingRegistry::FindingRegistry(Limits limits) : limits_(limits) {}

Result<FindingId> FindingRegistry::record(const ComparisonResult& result, TimePoint now) {
  if (result.primary == DivergenceClass::None) {
    return FindingId{};
  }

  Finding candidate;
  candidate.cls = result.primary;
  candidate.group = result.group;
  candidate.path = result.path.present() ? result.path : result.matched_path;
  candidate.generation = result.generation;
  candidate.topology = result.topology;
  candidate.last_comparison = result.id;
  candidate.detail = std::string("primary class ") + to_code(result.primary);
  for (const auto& judgement : result.judgements) {
    if (judgement.accepted) {
      candidate.evidence.push_back(judgement.id);
    }
  }
  if (candidate.evidence.size() > limits_.max_finding_evidence) {
    candidate.evidence.resize(limits_.max_finding_evidence);
  }
  for (const auto& difference : result.differences) {
    candidate.hop_indices.push_back(difference.index);
  }
  candidate.seal();

  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = findings_.find(candidate.id);
  if (existing != findings_.end()) {
    Finding& finding = existing->second;
    ++finding.occurrences;
    finding.last_seen = now;
    finding.last_comparison = result.id;
    if (finding.status == FindingStatus::Resolved) {
      // The defect is observable again: a resolved finding reopens rather than
      // staying silently closed.
      finding.status = FindingStatus::Open;
    }
    return finding.id;
  }

  if (findings_.size() >= limits_.max_findings) {
    // Evict the least recently seen resolved finding; if none is resolved the
    // registry is genuinely full and the caller must be told.
    auto victim = findings_.end();
    for (auto it = findings_.begin(); it != findings_.end(); ++it) {
      if (it->second.status != FindingStatus::Resolved) {
        continue;
      }
      if (victim == findings_.end() || it->second.last_seen < victim->second.last_seen ||
          (it->second.last_seen == victim->second.last_seen && it->first < victim->first)) {
        victim = it;
      }
    }
    if (victim == findings_.end()) {
      return Error{ErrorCode::CapacityExceeded,
                   "finding registry is full and holds no resolved finding to evict"};
    }
    findings_.erase(victim);
    ++evicted_;
  }

  candidate.occurrences = 1;
  candidate.first_seen = now;
  candidate.last_seen = now;
  candidate.status = FindingStatus::Open;
  const FindingId id = candidate.id;
  findings_.emplace(id, std::move(candidate));
  return id;
}

Result<Finding> FindingRegistry::find(FindingId candidate) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = findings_.find(candidate);
  if (it == findings_.end()) {
    return Error{ErrorCode::UnknownIdentity, "no such finding", candidate.str()};
  }
  return it->second;
}

bool FindingRegistry::contains(FindingId candidate) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findings_.find(candidate) != findings_.end();
}

Status FindingRegistry::acknowledge(FindingId candidate, TimePoint now) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = findings_.find(candidate);
  if (it == findings_.end()) {
    return Error{ErrorCode::UnknownIdentity, "no such finding", candidate.str()};
  }
  if (it->second.status == FindingStatus::Open) {
    it->second.status = FindingStatus::Acknowledged;
    it->second.last_seen = now;
  }
  return ok();
}

Result<std::size_t> FindingRegistry::resolve_from_match(const ComparisonResult& result,
                                                        TimePoint now) {
  if (result.outcome != MatchOutcome::Match) {
    return static_cast<std::size_t>(0);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t resolved = 0;
  for (auto& entry : findings_) {
    Finding& finding = entry.second;
    if (!finding.is_open()) {
      continue;
    }
    const bool same_subject =
        (result.group.present() && finding.group.present() && result.group.value() == finding.group.value()) ||
        (result.path.present() && finding.path.present() && result.path.value() == finding.path.value());
    if (!same_subject) {
      continue;
    }
    if (finding.generation > result.generation) {
      // A match against an older generation says nothing about a finding that
      // belongs to a newer one.
      continue;
    }
    finding.status = FindingStatus::Resolved;
    finding.last_seen = now;
    ++resolved;
  }
  return resolved;
}

Status FindingRegistry::restore(const Finding& finding) {
  const FindingId recomputed = FindingId::from_digest(finding.identity_digest());
  if (recomputed != finding.id) {
    return Error{ErrorCode::IntegrityFailure, "restored finding identity does not match content"};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = findings_.find(finding.id);
  if (existing != findings_.end()) {
    if (finder_keeps_newest(existing->second, finding)) {
      existing->second = finding;
    }
    return ok();
  }
  if (findings_.size() >= limits_.max_findings) {
    return Error{ErrorCode::CapacityExceeded, "finding registry is full"};
  }
  findings_.emplace(finding.id, finding);
  return ok();
}

std::vector<Finding> FindingRegistry::all() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Finding> out;
  out.reserve(findings_.size());
  for (const auto& entry : findings_) {
    out.push_back(entry.second);
  }
  return out;
}

std::size_t FindingRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findings_.size();
}

std::size_t FindingRegistry::open_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t count = 0;
  for (const auto& entry : findings_) {
    if (entry.second.is_open()) {
      ++count;
    }
  }
  return count;
}

std::uint64_t FindingRegistry::evicted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evicted_;
}

void FindingRegistry::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  findings_.clear();
}

} // namespace pathobs
