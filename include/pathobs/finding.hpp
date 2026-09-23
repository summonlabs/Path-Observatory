// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Findings.
//
// A finding is the durable form of a divergence class observed for a subject.
// Its identity is a digest over the class, the subject, the generations and the
// sorted set of participating evidence identities. Times, occurrence counts and
// lifecycle state are deliberately excluded: the same defect seen twice is the
// same finding, not a second one.

#ifndef PATHOBS_FINDING_HPP
#define PATHOBS_FINDING_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/compare.hpp"
#include "pathobs/divergence.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pathobs {

enum class FindingStatus : std::uint8_t {
  Open = 0,
  Acknowledged = 1,
  Resolved = 2,
};

PATHOBS_API const char* to_string(FindingStatus status) noexcept;
PATHOBS_API Result<FindingStatus> parse_finding_status(std::string_view text);

struct Finding {
  FindingId id{};
  DivergenceClass cls{DivergenceClass::None};
  MaybeId<PathGroupId> group{};
  MaybeId<PathId> path{};
  RouteGenerationId generation{};
  TopologyGenerationId topology{};
  /// Participating evidence identities, ascending.
  std::vector<EvidenceId> evidence{};
  /// Hop positions that diverged, ascending.
  std::vector<std::uint32_t> hop_indices{};
  FindingStatus status{FindingStatus::Open};
  std::uint64_t occurrences{0};
  TimePoint first_seen{};
  TimePoint last_seen{};
  ComparisonId last_comparison{};
  std::string detail;

  /// Identity is content derived over the stable tuple only.
  void seal();
  Digest identity_digest() const;

  bool is_open() const noexcept { return status != FindingStatus::Resolved; }
};

class PATHOBS_API FindingRegistry {
 public:
  explicit FindingRegistry(Limits limits);

  /// Fold a comparison result into the registry. Returns the identity of the
  /// finding created or updated. A result whose primary class is None produces
  /// no finding and returns a zero identity.
  Result<FindingId> record(const ComparisonResult& result, TimePoint now);

  Result<Finding> find(FindingId candidate) const;
  bool contains(FindingId candidate) const;
  Status acknowledge(FindingId candidate, TimePoint now);

  /// Mark findings for the same subject at or below the generation of a
  /// successful comparison as resolved. Returns how many were resolved.
  Result<std::size_t> resolve_from_match(const ComparisonResult& result, TimePoint now);

  /// Reinstate a finding loaded from a store. The occurrence count, first seen
  /// time and lifecycle state are preserved rather than reset, because a
  /// finding that was acknowledged before a restart is still acknowledged.
  Status restore(const Finding& finding);

  std::vector<Finding> all() const;
  std::size_t size() const;
  std::size_t open_count() const;
  std::uint64_t evicted() const;
  void clear();

 private:
  mutable std::mutex mutex_;
  Limits limits_;
  std::map<FindingId, Finding> findings_;
  std::uint64_t evicted_{0};
};

} // namespace pathobs

#endif // PATHOBS_FINDING_HPP
