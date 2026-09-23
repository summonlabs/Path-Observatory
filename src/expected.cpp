// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/expected.hpp"

#include "pathobs/observed.hpp"

#include <algorithm>

namespace pathobs {

Digest ExpectedPath::canonical_digest() const {
  CanonicalWriter writer;
  writer.text("pathobs.expected_path");
  writer.id(id);
  writer.id(group);
  writer.id(generation);
  writer.count(hops.size());
  for (const auto& hop : hops) {
    writer.id(hop.id);
    writer.u32(hop.index);
    writer.id(hop.device);
    writer.id(hop.ingress);
    writer.id(hop.egress);
    writer.boolean(hop.has_egress_link);
    writer.id(hop.egress_link);
  }
  return writer.digest();
}

Digest RouteGeneration::canonical_digest() const {
  CanonicalWriter writer;
  writer.text("pathobs.route_generation");
  writer.id(id);
  writer.id(epoch);
  writer.id(incarnation);
  writer.id(revision);
  writer.id(sequence);
  writer.id(source);
  writer.id(topology);
  writer.i64(created.unix_nanos());
  writer.count(groups.size());
  for (const auto& group : groups) {
    writer.id(group.id);
    writer.id(group.origin);
    writer.id(group.destination);
    writer.text(group.label);
    writer.count(group.members.size());
    for (const auto member : group.members) {
      writer.id(member);
    }
  }
  writer.count(paths.size());
  for (const auto& path : paths) {
    writer.digest(path.canonical_digest());
  }
  return writer.digest();
}

Result<void> RouteGeneration::finalize(const Limits& limits) {
  if (!id.valid() || !epoch.valid() || !incarnation.valid() || !revision.valid() || !source.valid() ||
      !topology.valid()) {
    return Error{ErrorCode::InvalidIdentity,
                 "route generation is missing one of generation, epoch, incarnation, revision, "
                 "source or topology"};
  }
  if (paths.size() > limits.max_paths_per_generation) {
    return Error{ErrorCode::CapacityExceeded, "route generation path count exceeds the bound"};
  }
  if (groups.size() > limits.max_groups_per_generation) {
    return Error{ErrorCode::CapacityExceeded, "route generation group count exceeds the bound"};
  }

  std::sort(paths.begin(), paths.end(),
            [](const ExpectedPath& a, const ExpectedPath& b) { return a.id < b.id; });
  std::sort(groups.begin(), groups.end(),
            [](const PathGroup& a, const PathGroup& b) { return a.id < b.id; });

  for (std::size_t i = 0; i < paths.size(); ++i) {
    ExpectedPath& path = paths[i];
    if (!path.id.valid() || !path.group.valid() || !path.generation.valid()) {
      return Error{ErrorCode::InvalidIdentity, "expected path is missing an identity"};
    }
    if (path.generation != id) {
      return Error{ErrorCode::InvalidArgument,
                   "expected path names a different route generation than its container"};
    }
    if (i > 0 && paths[i - 1].id == path.id) {
      return Error{ErrorCode::DuplicateIdentity, "route generation contains a duplicate path"};
    }
    if (path.hops.empty()) {
      return Error{ErrorCode::InvalidArgument, "expected path has no hops", path.id.str()};
    }
    if (path.hops.size() > limits.max_hops_per_path) {
      return Error{ErrorCode::CapacityExceeded, "expected path exceeds the hop bound", path.id.str()};
    }
    for (std::size_t hop_index = 0; hop_index < path.hops.size(); ++hop_index) {
      ExpectedHop& hop = path.hops[hop_index];
      if (!hop.id.valid() || !hop.device.valid() || !hop.ingress.valid() || !hop.egress.valid()) {
        return Error{ErrorCode::InvalidIdentity, "expected hop is missing an identity",
                     path.id.str()};
      }
      if (hop.index != hop_index) {
        return Error{ErrorCode::InvalidArgument,
                     "expected hop index does not match its position", path.id.str()};
      }
    }
    path.digest = path.canonical_digest();
  }

  for (std::size_t i = 0; i < groups.size(); ++i) {
    PathGroup& group = groups[i];
    if (!group.id.valid() || !group.origin.valid() || !group.destination.valid()) {
      return Error{ErrorCode::InvalidIdentity, "path group is missing an identity"};
    }
    if (i > 0 && groups[i - 1].id == group.id) {
      return Error{ErrorCode::DuplicateIdentity, "route generation contains a duplicate group"};
    }
    if (group.members.size() > limits.max_members_per_group) {
      return Error{ErrorCode::CapacityExceeded, "path group exceeds the member bound",
                   group.id.str()};
    }
    std::sort(group.members.begin(), group.members.end());
    group.members.erase(std::unique(group.members.begin(), group.members.end()),
                        group.members.end());
    if (group.members.empty()) {
      return Error{ErrorCode::InvalidArgument, "path group has no members", group.id.str()};
    }
    for (const auto member : group.members) {
      const auto it = std::lower_bound(paths.begin(), paths.end(), member,
                                       [](const ExpectedPath& path, PathId value) {
                                         return path.id < value;
                                       });
      if (it == paths.end() || it->id != member) {
        return Error{ErrorCode::UnknownIdentity,
                     "path group references a path that is not in the generation", member.str()};
      }
      if (it->group != group.id) {
        return Error{ErrorCode::InvalidArgument,
                     "path group and path disagree about group membership", member.str()};
      }
    }
  }

  digest = canonical_digest();
  return ok();
}

const ExpectedPath* RouteGeneration::find_path(PathId candidate) const {
  const auto it = std::lower_bound(paths.begin(), paths.end(), candidate,
                                   [](const ExpectedPath& path, PathId value) {
                                     return path.id < value;
                                   });
  return it != paths.end() && it->id == candidate ? &*it : nullptr;
}

const PathGroup* RouteGeneration::find_group(PathGroupId candidate) const {
  const auto it = std::lower_bound(groups.begin(), groups.end(), candidate,
                                   [](const PathGroup& group, PathGroupId value) {
                                     return group.id < value;
                                   });
  return it != groups.end() && it->id == candidate ? &*it : nullptr;
}

const PathGroup* RouteGeneration::find_group_by_endpoints(DeviceId origin, DeviceId destination,
                                                          bool* ambiguous) const {
  const PathGroup* found = nullptr;
  std::size_t matches = 0;
  for (const auto& group : groups) {
    if (group.origin == origin && group.destination == destination) {
      ++matches;
      if (found == nullptr) {
        found = &group;
      }
    }
  }
  if (ambiguous != nullptr) {
    *ambiguous = matches > 1;
  }
  return found;
}

namespace {

bool hops_agree(const ExpectedHop& expected, const ObservedHop& observed, std::uint32_t* difference) {
  if (expected.device != observed.device) {
    *difference = expected.index;
    return false;
  }
  // A port identity of zero in the observation means "not reported". Reporting
  // nothing is not the same as reporting a different port, so the comparison
  // defers to the completeness rules in the engine rather than inventing a
  // port mismatch here.
  if (observed.ingress.valid() && expected.ingress != observed.ingress) {
    *difference = expected.index;
    return false;
  }
  if (observed.egress.valid() && expected.egress != observed.egress) {
    *difference = expected.index;
    return false;
  }
  if (expected.has_egress_link && observed.link.valid() &&
      observed.link_provenance != HopLinkProvenance::Absent && expected.egress_link != observed.link) {
    *difference = expected.index;
    return false;
  }
  return true;
}

} // namespace

TraceComparison compare_trace(const ExpectedPath& expected,
                              const std::vector<ObservedHop>& observed) {
  TraceComparison result;
  if (expected.hops.empty() || observed.empty()) {
    result.match = TraceMatch::Incomparable;
    return result;
  }

  const std::size_t comparable = std::min(expected.hops.size(), observed.size());
  for (std::size_t i = 0; i < comparable; ++i) {
    std::uint32_t difference = 0;
    if (!hops_agree(expected.hops[i], observed[i], &difference)) {
      result.match = TraceMatch::Mismatch;
      result.first_difference = difference;
      result.has_difference = true;
      return result;
    }
  }

  if (observed.size() < expected.hops.size()) {
    result.match = TraceMatch::Prefix;
    return result;
  }
  result.match = TraceMatch::Exact;
  return result;
}

} // namespace pathobs
