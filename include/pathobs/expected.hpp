// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Expected path state.
//
// This is the model of what a routing authority says the path should be. Path
// Observatory consumes it and never produces it: nothing here computes a route,
// selects an alternate, or mutates forwarding. The model exists so that an
// observation can be compared against a declared plan, with the generation,
// epoch, incarnation and revision that the plan belongs to recorded explicitly.

#ifndef PATHOBS_EXPECTED_HPP
#define PATHOBS_EXPECTED_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/source.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"
#include "pathobs/topology.hpp"

#include <string>
#include <vector>

namespace pathobs {

/// One declared traversal of a device: traffic enters on the ingress port and
/// leaves on the egress port. The identity is stable for the lifetime of the
/// route generation that owns it.
struct ExpectedHop {
  HopId id{};
  std::uint32_t index{0};
  DeviceId device{};
  PortId ingress{};
  PortId egress{};
  /// Link used to reach the next hop. Declared by the plan only when the plan
  /// names it; absence is not an error and not a claim that no link exists.
  LinkId egress_link{};
  bool has_egress_link{false};

  friend bool operator==(const ExpectedHop&, const ExpectedHop&) noexcept = default;
};

struct ExpectedPath {
  PathId id{};
  PathGroupId group{};
  RouteGenerationId generation{};
  std::vector<ExpectedHop> hops;
  Digest digest{};

  friend bool operator==(const ExpectedPath&, const ExpectedPath&) noexcept = default;

  Digest canonical_digest() const;
};

/// A set of paths sharing an origin and a destination. More than one member is
/// legitimate multipath: a plan is allowed to declare several acceptable ways
/// to reach the same destination, and observing one of them is not divergence.
struct PathGroup {
  PathGroupId id{};
  DeviceId origin{};
  DeviceId destination{};
  std::string label;
  /// Member path identities, ascending. Sorted by finalize().
  std::vector<PathId> members;

  friend bool operator==(const PathGroup&, const PathGroup&) noexcept = default;

  bool multipath() const noexcept { return members.size() > 1; }
};

/// One generation of expected path state from a routing authority.
struct RouteGeneration {
  RouteGenerationId id{};
  EpochId epoch{};
  IncarnationId incarnation{};
  RevisionId revision{};
  SourceSequence sequence{};
  SourceId source{};
  TopologyGenerationId topology{};
  TimePoint created{};
  TimePoint received{};
  std::vector<PathGroup> groups;
  std::vector<ExpectedPath> paths;
  Digest digest{};

  Result<void> finalize(const Limits& limits);

  const ExpectedPath* find_path(PathId id) const;
  const PathGroup* find_group(PathGroupId id) const;
  /// Resolve a group by its endpoints. Returns nullptr when no group matches,
  /// and sets ambiguous when more than one does.
  const PathGroup* find_group_by_endpoints(DeviceId origin, DeviceId destination,
                                           bool* ambiguous) const;

  Digest canonical_digest() const;
};

/// How well an observed hop trace agrees with one declared path.
enum class TraceMatch : std::uint8_t {
  /// Every observed hop agrees and the observation covers the whole path.
  Exact = 0,
  /// Every observed hop agrees but the observation stops before the end.
  Prefix = 1,
  /// The observation disagrees at a specific hop position.
  Mismatch = 2,
  /// Nothing could be compared (no hops on one side).
  Incomparable = 3,
};

struct TraceComparison {
  TraceMatch match{TraceMatch::Incomparable};
  std::uint32_t first_difference{0};
  bool has_difference{false};
};

/// Compare an observed hop trace against one declared path. Pure and
/// deterministic; link identity is compared only when both sides state one.
PATHOBS_API TraceComparison compare_trace(const ExpectedPath& expected,
                                          const std::vector<struct ObservedHop>& observed);

} // namespace pathobs

#endif // PATHOBS_EXPECTED_HPP
