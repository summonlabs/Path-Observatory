// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/synthetic.hpp"

#include "pathobs/random.hpp"

#include <string>

namespace pathobs::synthetic {
namespace {

constexpr std::uint64_t kFirstDevice = 10;
constexpr std::uint64_t kLastDevice = 40;
constexpr std::uint64_t kMiddleDeviceBase = 100;
constexpr std::uint64_t kDivergentDevice = 9999;
constexpr std::uint64_t kLinkBase = 1000;
constexpr std::int64_t kCreationNanos = 1767225600000000000LL;  // 2026-01-01T00:00:00Z

DeviceId device_for(std::uint32_t member, std::uint32_t position, std::uint32_t hop_count) {
  if (position == 0) {
    return DeviceId::from_value(kFirstDevice);
  }
  if (position + 1 == hop_count) {
    return DeviceId::from_value(kLastDevice);
  }
  return DeviceId::from_value(kMiddleDeviceBase +
                              static_cast<std::uint64_t>(member) * 100 +
                              static_cast<std::uint64_t>(position));
}

/// Port identity of one member's use of a device. Members that share a device
/// (the origin and the destination of a multipath group always do) must use
/// different ports, otherwise two links would sit on one endpoint and link
/// correlation would have to call the result ambiguous.
PortId ingress_for(DeviceId device, std::uint32_t member) {
  return PortId::from_value(device.value() * 100 + static_cast<std::uint64_t>(member) * 10 + 1);
}

PortId egress_for(DeviceId device, std::uint32_t member) {
  return PortId::from_value(device.value() * 100 + static_cast<std::uint64_t>(member) * 10 + 2);
}

LinkId link_for(std::uint32_t member, std::uint32_t position) {
  return LinkId::from_value(kLinkBase + static_cast<std::uint64_t>(member) * 100 + position);
}

} // namespace

DeviceId divergent_device_id() noexcept { return DeviceId::from_value(kDivergentDevice); }

Result<LabFabric> build_lab_fabric(const LabOptions& options, const Limits& limits) {
  if (options.hop_count < 2) {
    return Error{ErrorCode::InvalidArgument, "a laboratory path needs at least two devices"};
  }
  if (options.hop_count > limits.max_hops_per_path) {
    return Error{ErrorCode::OutOfRange, "the requested hop count exceeds the configured bound"};
  }
  if (options.multipath_members == 0) {
    return Error{ErrorCode::InvalidArgument, "a path group needs at least one member"};
  }
  if (options.multipath_members > limits.max_members_per_group) {
    return Error{ErrorCode::OutOfRange, "the requested member count exceeds the configured bound"};
  }
  if (options.evidence_count > limits.max_evidence_per_comparison) {
    return Error{ErrorCode::OutOfRange, "the requested evidence count exceeds the configured bound"};
  }

  LabFabric fabric;
  const TimePoint created = TimePoint::from_unix_nanos(kCreationNanos);
  const TimePoint observed = TimePoint::from_unix_nanos(
      kCreationNanos + options.evidence_age_nanos);

  SourceDescriptor planning;
  planning.id = SourceId::from_value(options.planning_source_id);
  planning.name = "laboratory-planning-authority";
  planning.kind = SourceKind::RoutingAuthority;
  planning.surface = ProofSurface::Synthetic;
  planning.authority = AuthorityClass::Authoritative;
  planning.capabilities.expected_state = true;
  planning.capabilities.topology = true;
  const Status planning_valid = planning.validate();
  if (!planning_valid.has_value()) {
    return planning_valid.error();
  }

  SourceDescriptor observer;
  observer.id = SourceId::from_value(options.observer_source_id);
  observer.name = "laboratory-observer";
  observer.kind = options.observer_kind;
  observer.surface = options.surface;
  observer.authority = options.observer_authority;
  observer.capabilities.observed_evidence = true;
  const Status observer_valid = observer.validate();
  if (!observer_valid.has_value()) {
    return observer_valid.error();
  }
  fabric.sources.push_back(planning);
  fabric.sources.push_back(observer);

  // ---------------------------------------------------------------------
  // Topology: every device, port and link used by any member.
  // ---------------------------------------------------------------------
  // When the caller asks for stale evidence the plan is moved one epoch ahead,
  // so the observation is genuinely behind the authority rather than simply
  // malformed. Epoch zero is not a valid identity, which is why the plan moves
  // instead of the evidence.
  const std::uint64_t plan_epoch = options.stale_epoch ? options.epoch + 1 : options.epoch;

  TopologyGeneration& topology = fabric.topology;
  topology.id = TopologyGenerationId::from_value(options.topology_generation);
  topology.epoch = EpochId::from_value(plan_epoch);
  topology.incarnation = IncarnationId::from_value(options.incarnation);
  topology.revision = RevisionId::from_value(2);
  topology.sequence = SourceSequence::from_value(9);
  topology.source = planning.id;
  topology.created = created;
  topology.received = created;

  const auto device_present = [&topology](DeviceId candidate) {
    for (const auto& existing : topology.devices) {
      if (existing.id == candidate) {
        return true;
      }
    }
    return false;
  };
  const auto port_present = [&topology](PortId candidate) {
    for (const auto& existing : topology.ports) {
      if (existing.id == candidate) {
        return true;
      }
    }
    return false;
  };

  for (std::uint32_t member = 0; member < options.multipath_members; ++member) {
    for (std::uint32_t position = 0; position < options.hop_count; ++position) {
      const DeviceId device = device_for(member, position, options.hop_count);
      if (!device_present(device)) {
        DeviceRecord record;
        record.id = device;
        record.name = "lab-device-" + std::to_string(device.value());
        record.role = DeviceRole::Switch;
        topology.devices.push_back(record);
      }
      for (std::uint64_t port_index = 1; port_index <= 2; ++port_index) {
        PortRecord port;
        port.id = PortId::from_value(device.value() * 100 +
                                     static_cast<std::uint64_t>(member) * 10 + port_index);
        port.device = device;
        port.name = "lab-port-" + std::to_string(port_index);
        port.index = static_cast<std::uint32_t>(port_index);
        if (!port_present(port.id)) {
          topology.ports.push_back(port);
        }
      }
      if (position + 1 < options.hop_count) {
        LinkRecord link;
        link.id = link_for(member, position);
        link.local_device = device;
        link.local_port = egress_for(device, member);
        const DeviceId next = device_for(member, position + 1, options.hop_count);
        link.remote_device = next;
        link.remote_port = ingress_for(next, member);
        link.kind = LinkKind::Physical;
        topology.links.push_back(link);
      }
    }
  }
  const Status topology_valid = topology.finalize(limits);
  if (!topology_valid.has_value()) {
    return topology_valid.error();
  }

  // ---------------------------------------------------------------------
  // Expected path state.
  // ---------------------------------------------------------------------
  RouteGeneration& routes = fabric.routes;
  routes.id = RouteGenerationId::from_value(options.route_generation);
  routes.epoch = topology.epoch;
  routes.incarnation = topology.incarnation;
  // The same trick as the epoch: a stale revision is expressed by moving the
  // plan forward, because revision zero is not a valid identity.
  routes.revision =
      RevisionId::from_value(options.stale_revision ? options.revision + 1 : options.revision);
  routes.sequence = SourceSequence::from_value(100);
  routes.source = planning.id;
  routes.topology = topology.id;
  routes.created = created;
  routes.received = created;

  PathGroup group;
  group.id = PathGroupId::from_value(1);
  group.origin = device_for(0, 0, options.hop_count);
  group.destination = device_for(0, options.hop_count - 1, options.hop_count);
  group.label = "laboratory path group";

  for (std::uint32_t member = 0; member < options.multipath_members; ++member) {
    const PathId path_id = PathId::from_value(100 + member);
    group.members.push_back(path_id);
    if (member == 0) {
      fabric.primary_path = path_id;
    }
    ExpectedPath path;
    path.id = path_id;
    path.group = group.id;
    path.generation = routes.id;
    for (std::uint32_t position = 0; position < options.hop_count; ++position) {
      ExpectedHop hop;
      hop.id = HopId::from_value(
          1000 + static_cast<std::uint64_t>(member) * 1000 + position);
      hop.index = position;
      hop.device = device_for(member, position, options.hop_count);
      hop.ingress = ingress_for(hop.device, member);
      hop.egress = egress_for(hop.device, member);
      if (position + 1 < options.hop_count) {
        hop.egress_link = link_for(member, position);
        hop.has_egress_link = true;
      }
      path.hops.push_back(hop);
    }
    routes.paths.push_back(std::move(path));
  }
  fabric.group = group.id;
  routes.groups.push_back(group);
  const Status routes_valid = routes.finalize(limits);
  if (!routes_valid.has_value()) {
    return routes_valid.error();
  }

  // ---------------------------------------------------------------------
  // Observed evidence.
  // ---------------------------------------------------------------------
  for (std::uint32_t index = 0; index < options.evidence_count; ++index) {
    ObservedPathEvidence evidence;
    evidence.source = observer.id;
    evidence.incarnation = SourceIncarnationId::from_value(7);
    evidence.sequence = SourceSequence::from_value(options.first_sequence + index);
    evidence.surface = options.surface;
    evidence.authority = options.observer_authority;
    evidence.path = MaybeId<PathId>(fabric.primary_path);
    evidence.group = MaybeId<PathGroupId>(group.id);
    evidence.origin = MaybeId<DeviceId>(group.origin);
    evidence.destination = MaybeId<DeviceId>(group.destination);
    evidence.epoch = EpochId::from_value(options.epoch);
    evidence.claimed_incarnation = topology.incarnation;
    evidence.route_generation =
        MaybeId<RouteGenerationId>(options.unknown_generation
                                       ? RouteGenerationId::from_value(options.route_generation + 100)
                                       : routes.id);
    evidence.topology_generation = MaybeId<TopologyGenerationId>(topology.id);
    evidence.revision = RevisionId::from_value(options.revision);
    evidence.observed_at = observed;
    evidence.received_at = observed;
    evidence.note = "laboratory observation";

    if (!options.empty_trace) {
      const std::uint32_t trace_length =
          options.partial ? (options.hop_count > 1 ? options.hop_count - 1 : 1) : options.hop_count;
      for (std::uint32_t position = 0; position < trace_length; ++position) {
        ObservedHop hop;
        hop.index = position;
        DeviceId device = device_for(0, position, options.hop_count);
        if (options.diverge && position == 1 && options.hop_count > 2) {
          device = divergent_device_id();
        }
        hop.device = device;
        hop.ingress = ingress_for(device, 0);
        hop.egress = egress_for(device, 0);
        if (!options.omit_links && position + 1 < options.hop_count && !options.diverge) {
          hop.link = link_for(0, position);
          hop.link_provenance = HopLinkProvenance::Observed;
        }
        evidence.hops.push_back(hop);
      }
    }
    evidence.source_truncated = options.partial;
    evidence.declared_completeness = options.partial ? EvidenceCompleteness::Partial
                                                     : EvidenceCompleteness::Complete;
    if (options.empty_trace) {
      evidence.declared_completeness = EvidenceCompleteness::Unknown;
    }
    evidence.seal();
    fabric.evidence.push_back(std::move(evidence));
  }

  fabric.divergent_device = divergent_device_id();
  return fabric;
}

} // namespace pathobs::synthetic
