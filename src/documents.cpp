// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/documents.hpp"

#include <algorithm>

namespace pathobs::documents {
namespace {

Result<const JsonValue*> require_member(const JsonValue& object, std::string_view key) {
  if (!object.is_object()) {
    return Error{ErrorCode::InvalidFormat, "document node is not an object"};
  }
  const JsonValue* value = object.find(key);
  if (value == nullptr) {
    return Error{ErrorCode::InvalidFormat,
                 "document is missing a required member: " + std::string(key)};
  }
  return value;
}

const JsonValue* optional_member(const JsonValue& object, std::string_view key) {
  return object.is_object() ? object.find(key) : nullptr;
}

Result<std::uint64_t> require_u64(const JsonValue& object, std::string_view key) {
  PATHOBS_TRY(value, require_member(object, key));
  if (!value->is_number()) {
    return Error{ErrorCode::InvalidFormat, "member is not a number: " + std::string(key)};
  }
  return value->as_u64();
}

Result<std::uint64_t> optional_u64(const JsonValue& object, std::string_view key,
                                   std::uint64_t fallback) {
  const JsonValue* value = optional_member(object, key);
  if (value == nullptr || value->is_null()) {
    return fallback;
  }
  if (!value->is_number()) {
    return Error{ErrorCode::InvalidFormat, "member is not a number: " + std::string(key)};
  }
  return value->as_u64();
}

Result<std::string> require_text(const JsonValue& object, std::string_view key) {
  PATHOBS_TRY(value, require_member(object, key));
  if (!value->is_string()) {
    return Error{ErrorCode::InvalidFormat, "member is not a string: " + std::string(key)};
  }
  return value->as_string();
}

Result<std::string> optional_text(const JsonValue& object, std::string_view key,
                                  std::string fallback) {
  const JsonValue* value = optional_member(object, key);
  if (value == nullptr || value->is_null()) {
    return fallback;
  }
  if (!value->is_string()) {
    return Error{ErrorCode::InvalidFormat, "member is not a string: " + std::string(key)};
  }
  return value->as_string();
}

Result<bool> optional_boolean(const JsonValue& object, std::string_view key, bool fallback) {
  const JsonValue* value = optional_member(object, key);
  if (value == nullptr || value->is_null()) {
    return fallback;
  }
  return value->as_bool_checked();
}

Result<TimePoint> require_time(const JsonValue& object, std::string_view key) {
  PATHOBS_TRY(text, require_text(object, key));
  return TimePoint::parse_rfc3339(text);
}

Result<TimePoint> optional_time(const JsonValue& object, std::string_view key, TimePoint fallback) {
  const JsonValue* value = optional_member(object, key);
  if (value == nullptr || value->is_null()) {
    return fallback;
  }
  if (!value->is_string()) {
    return Error{ErrorCode::InvalidFormat, "member is not a timestamp: " + std::string(key)};
  }
  PATHOBS_TRY(parsed, TimePoint::parse_rfc3339(value->as_string()));
  return parsed;
}

Status check_kind(const JsonValue& object, std::string_view expected) {
  PATHOBS_TRY(kind, require_text(object, "kind"));
  if (kind != expected) {
    return Error{ErrorCode::InvalidFormat, "document kind does not match", kind};
  }
  return ok();
}

template <class Id>
Result<Id> to_id(std::uint64_t raw, const char* what) {
  if (raw == 0) {
    return Error{ErrorCode::InvalidIdentity,
                 std::string("identity must not be zero: ") + what};
  }
  return Id::from_value(raw);
}

Result<MaybeId<DeviceId>> optional_device(const JsonValue& object, std::string_view key) {
  PATHOBS_TRY(raw, optional_u64(object, key, 0));
  if (raw == 0) {
    return MaybeId<DeviceId>{};
  }
  return MaybeId<DeviceId>(DeviceId::from_value(raw));
}

void set_string(JsonValue& object, const char* key, std::string value) {
  object.set(key, JsonValue::make_string(std::move(value)));
}

void set_bool(JsonValue& object, const char* key, bool value) {
  object.set(key, JsonValue::make_boolean(value));
}

void set_u64(JsonValue& object, const char* key, std::uint64_t value) {
  object.set(key, JsonValue::make_number(value));
}

void set_time(JsonValue& object, const char* key, TimePoint value) {
  object.set(key, JsonValue::make_string(value.to_rfc3339()));
}

JsonValue hop_json(const ExpectedHop& hop) {
  JsonValue object = JsonValue::make_object();
  set_u64(object, "id", hop.id.value());
  set_u64(object, "index", hop.index);
  set_u64(object, "device", hop.device.value());
  set_u64(object, "ingress", hop.ingress.value());
  set_u64(object, "egress", hop.egress.value());
  if (hop.has_egress_link) {
    set_u64(object, "link", hop.egress_link.value());
  }
  return object;
}

JsonValue observed_hop_json(const ObservedHop& hop) {
  JsonValue object = JsonValue::make_object();
  set_u64(object, "index", hop.index);
  set_u64(object, "device", hop.device.value());
  if (hop.ingress.valid()) {
    set_u64(object, "ingress", hop.ingress.value());
  }
  if (hop.egress.valid()) {
    set_u64(object, "egress", hop.egress.value());
  }
  if (hop.link.valid()) {
    set_u64(object, "link", hop.link.value());
  }
  set_string(object, "link_provenance", to_string(hop.link_provenance));
  if (hop.at.is_set()) {
    set_time(object, "at", hop.at);
  }
  return object;
}

} // namespace

Result<SourceDescriptor> parse_source_descriptor(std::string_view text, const Limits& limits) {
  PATHOBS_TRY(document, JsonValue::parse(text, limits));
  const Status kind_ok = check_kind(document, kSourceKindTag);
  if (!kind_ok.has_value()) {
    return kind_ok.error();
  }

  SourceDescriptor descriptor;
  PATHOBS_TRY(id_raw, require_u64(document, "id"));
  PATHOBS_TRY(id, to_id<SourceId>(id_raw, "source"));
  descriptor.id = id;
  PATHOBS_TRY(name, require_text(document, "name"));
  descriptor.name = std::move(name);
  PATHOBS_TRY(kind_text, require_text(document, "source_kind"));
  PATHOBS_TRY(kind, parse_source_kind(kind_text));
  descriptor.kind = kind;
  PATHOBS_TRY(surface_text, require_text(document, "surface"));
  PATHOBS_TRY(surface, parse_proof_surface(surface_text));
  descriptor.surface = surface;
  PATHOBS_TRY(authority_text, require_text(document, "authority"));
  PATHOBS_TRY(authority, parse_authority_class(authority_text));
  descriptor.authority = authority;

  const JsonValue* capabilities = optional_member(document, "capabilities");
  if (capabilities != nullptr) {
    if (!capabilities->is_array()) {
      return Error{ErrorCode::InvalidFormat, "capabilities must be an array"};
    }
    for (const auto& item : capabilities->items()) {
      if (!item.is_string()) {
        return Error{ErrorCode::InvalidFormat, "capability names must be strings"};
      }
      const std::string& capability = item.as_string();
      if (capability == "expected_state") {
        descriptor.capabilities.expected_state = true;
      } else if (capability == "observed_evidence") {
        descriptor.capabilities.observed_evidence = true;
      } else if (capability == "topology") {
        descriptor.capabilities.topology = true;
      } else {
        return Error{ErrorCode::InvalidFormat, "unknown capability", capability};
      }
    }
  }

  PATHOBS_TRY(age_ms, optional_u64(document, "max_observation_age_ms", 0));
  PATHOBS_TRY(skew_ms, optional_u64(document, "clock_skew_tolerance_ms", 0));
  if (age_ms > 86400000ull || skew_ms > 86400000ull) {
    return Error{ErrorCode::OutOfRange, "source timing tolerance is implausibly large"};
  }
  descriptor.max_observation_age = Nanoseconds{static_cast<std::int64_t>(age_ms) * 1000000};
  descriptor.clock_skew_tolerance = Nanoseconds{static_cast<std::int64_t>(skew_ms) * 1000000};

  const Status valid = descriptor.validate();
  if (!valid.has_value()) {
    return valid.error();
  }
  return descriptor;
}

Result<TopologyGeneration> parse_topology_generation(std::string_view text, const Limits& limits) {
  PATHOBS_TRY(document, JsonValue::parse(text, limits));
  const Status kind_ok = check_kind(document, kTopologyKindTag);
  if (!kind_ok.has_value()) {
    return kind_ok.error();
  }

  TopologyGeneration generation;
  PATHOBS_TRY(generation_raw, require_u64(document, "generation"));
  PATHOBS_TRY(generation_id, to_id<TopologyGenerationId>(generation_raw, "topology generation"));
  generation.id = generation_id;
  PATHOBS_TRY(epoch_raw, require_u64(document, "epoch"));
  PATHOBS_TRY(epoch, to_id<EpochId>(epoch_raw, "epoch"));
  generation.epoch = epoch;
  PATHOBS_TRY(incarnation_raw, require_u64(document, "incarnation"));
  PATHOBS_TRY(incarnation, to_id<IncarnationId>(incarnation_raw, "incarnation"));
  generation.incarnation = incarnation;
  PATHOBS_TRY(revision_raw, require_u64(document, "revision"));
  PATHOBS_TRY(revision, to_id<RevisionId>(revision_raw, "revision"));
  generation.revision = revision;
  PATHOBS_TRY(sequence_raw, require_u64(document, "sequence"));
  PATHOBS_TRY(sequence, to_id<SourceSequence>(sequence_raw, "sequence"));
  generation.sequence = sequence;
  PATHOBS_TRY(source_raw, require_u64(document, "source"));
  PATHOBS_TRY(source, to_id<SourceId>(source_raw, "source"));
  generation.source = source;
  PATHOBS_TRY(created, require_time(document, "created"));
  generation.created = created;
  PATHOBS_TRY(received, optional_time(document, "received", created));
  generation.received = received;

  const JsonValue* devices = optional_member(document, "devices");
  if (devices != nullptr) {
    if (!devices->is_array()) {
      return Error{ErrorCode::InvalidFormat, "devices must be an array"};
    }
    if (devices->items().size() > limits.max_devices_per_topology) {
      return Error{ErrorCode::CapacityExceeded, "device list exceeds the configured bound"};
    }
    for (const auto& item : devices->items()) {
      DeviceRecord record;
      PATHOBS_TRY(raw, require_u64(item, "id"));
      PATHOBS_TRY(id, to_id<DeviceId>(raw, "device"));
      record.id = id;
      PATHOBS_TRY(name, optional_text(item, "name", std::string{}));
      record.name = std::move(name);
      PATHOBS_TRY(role_text, optional_text(item, "role", std::string("unknown")));
      PATHOBS_TRY(role, parse_device_role(role_text));
      record.role = role;
      generation.devices.push_back(std::move(record));
    }
  }

  const JsonValue* ports = optional_member(document, "ports");
  if (ports != nullptr) {
    if (!ports->is_array()) {
      return Error{ErrorCode::InvalidFormat, "ports must be an array"};
    }
    if (ports->items().size() > limits.max_links_per_topology * 2ull) {
      return Error{ErrorCode::CapacityExceeded, "port list exceeds the configured bound"};
    }
    for (const auto& item : ports->items()) {
      PortRecord record;
      PATHOBS_TRY(raw, require_u64(item, "id"));
      PATHOBS_TRY(id, to_id<PortId>(raw, "port"));
      record.id = id;
      PATHOBS_TRY(device_raw, require_u64(item, "device"));
      PATHOBS_TRY(device, to_id<DeviceId>(device_raw, "device"));
      record.device = device;
      PATHOBS_TRY(name, optional_text(item, "name", std::string{}));
      record.name = std::move(name);
      PATHOBS_TRY(index, optional_u64(item, "index", 0));
      record.index = static_cast<std::uint32_t>(index);
      generation.ports.push_back(std::move(record));
    }
  }

  const JsonValue* links = optional_member(document, "links");
  if (links != nullptr) {
    if (!links->is_array()) {
      return Error{ErrorCode::InvalidFormat, "links must be an array"};
    }
    if (links->items().size() > limits.max_links_per_topology) {
      return Error{ErrorCode::CapacityExceeded, "link list exceeds the configured bound"};
    }
    for (const auto& item : links->items()) {
      LinkRecord record;
      PATHOBS_TRY(raw, require_u64(item, "id"));
      PATHOBS_TRY(id, to_id<LinkId>(raw, "link"));
      record.id = id;
      PATHOBS_TRY(local_device_raw, require_u64(item, "local_device"));
      PATHOBS_TRY(local_device, to_id<DeviceId>(local_device_raw, "local device"));
      record.local_device = local_device;
      PATHOBS_TRY(local_port_raw, require_u64(item, "local_port"));
      PATHOBS_TRY(local_port, to_id<PortId>(local_port_raw, "local port"));
      record.local_port = local_port;
      PATHOBS_TRY(remote_device_raw, require_u64(item, "remote_device"));
      PATHOBS_TRY(remote_device, to_id<DeviceId>(remote_device_raw, "remote device"));
      record.remote_device = remote_device;
      PATHOBS_TRY(remote_port_raw, require_u64(item, "remote_port"));
      PATHOBS_TRY(remote_port, to_id<PortId>(remote_port_raw, "remote port"));
      record.remote_port = remote_port;
      PATHOBS_TRY(kind_text, optional_text(item, "kind", std::string("unknown")));
      PATHOBS_TRY(kind, parse_link_kind(kind_text));
      record.kind = kind;
      generation.links.push_back(std::move(record));
    }
  }

  const Status finalized = generation.finalize(limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  return generation;
}

Result<RouteGeneration> parse_route_generation(std::string_view text, const Limits& limits) {
  PATHOBS_TRY(document, JsonValue::parse(text, limits));
  const Status kind_ok = check_kind(document, kRoutesKindTag);
  if (!kind_ok.has_value()) {
    return kind_ok.error();
  }

  RouteGeneration generation;
  PATHOBS_TRY(generation_raw, require_u64(document, "generation"));
  PATHOBS_TRY(generation_id, to_id<RouteGenerationId>(generation_raw, "route generation"));
  generation.id = generation_id;
  PATHOBS_TRY(epoch_raw, require_u64(document, "epoch"));
  PATHOBS_TRY(epoch, to_id<EpochId>(epoch_raw, "epoch"));
  generation.epoch = epoch;
  PATHOBS_TRY(incarnation_raw, require_u64(document, "incarnation"));
  PATHOBS_TRY(incarnation, to_id<IncarnationId>(incarnation_raw, "incarnation"));
  generation.incarnation = incarnation;
  PATHOBS_TRY(revision_raw, require_u64(document, "revision"));
  PATHOBS_TRY(revision, to_id<RevisionId>(revision_raw, "revision"));
  generation.revision = revision;
  PATHOBS_TRY(sequence_raw, require_u64(document, "sequence"));
  PATHOBS_TRY(sequence, to_id<SourceSequence>(sequence_raw, "sequence"));
  generation.sequence = sequence;
  PATHOBS_TRY(source_raw, require_u64(document, "source"));
  PATHOBS_TRY(source, to_id<SourceId>(source_raw, "source"));
  generation.source = source;
  PATHOBS_TRY(topology_raw, require_u64(document, "topology_generation"));
  PATHOBS_TRY(topology, to_id<TopologyGenerationId>(topology_raw, "topology generation"));
  generation.topology = topology;
  PATHOBS_TRY(created, require_time(document, "created"));
  generation.created = created;
  PATHOBS_TRY(received, optional_time(document, "received", created));
  generation.received = received;

  PATHOBS_TRY(groups_value, require_member(document, "groups"));
  if (!groups_value->is_array()) {
    return Error{ErrorCode::InvalidFormat, "groups must be an array"};
  }
  if (groups_value->items().size() > limits.max_groups_per_generation) {
    return Error{ErrorCode::CapacityExceeded, "group list exceeds the configured bound"};
  }
  for (const auto& item : groups_value->items()) {
    PathGroup group;
    PATHOBS_TRY(raw, require_u64(item, "id"));
    PATHOBS_TRY(id, to_id<PathGroupId>(raw, "path group"));
    group.id = id;
    PATHOBS_TRY(origin_raw, require_u64(item, "origin"));
    PATHOBS_TRY(origin, to_id<DeviceId>(origin_raw, "origin device"));
    group.origin = origin;
    PATHOBS_TRY(destination_raw, require_u64(item, "destination"));
    PATHOBS_TRY(destination, to_id<DeviceId>(destination_raw, "destination device"));
    group.destination = destination;
    PATHOBS_TRY(label, optional_text(item, "label", std::string{}));
    group.label = std::move(label);
    PATHOBS_TRY(members, require_member(item, "members"));
    if (!members->is_array()) {
      return Error{ErrorCode::InvalidFormat, "group members must be an array"};
    }
    if (members->items().size() > limits.max_members_per_group) {
      return Error{ErrorCode::CapacityExceeded, "group exceeds the configured member bound"};
    }
    for (const auto& member : members->items()) {
      if (!member.is_number()) {
        return Error{ErrorCode::InvalidFormat, "group member identities must be numbers"};
      }
      PATHOBS_TRY(member_raw, member.as_u64());
      PATHOBS_TRY(member_id, to_id<PathId>(member_raw, "path"));
      group.members.push_back(member_id);
    }
    generation.groups.push_back(std::move(group));
  }

  PATHOBS_TRY(paths_value, require_member(document, "paths"));
  if (!paths_value->is_array()) {
    return Error{ErrorCode::InvalidFormat, "paths must be an array"};
  }
  if (paths_value->items().size() > limits.max_paths_per_generation) {
    return Error{ErrorCode::CapacityExceeded, "path list exceeds the configured bound"};
  }
  for (const auto& item : paths_value->items()) {
    ExpectedPath path;
    PATHOBS_TRY(raw, require_u64(item, "id"));
    PATHOBS_TRY(id, to_id<PathId>(raw, "path"));
    path.id = id;
    PATHOBS_TRY(group_raw, require_u64(item, "group"));
    PATHOBS_TRY(group, to_id<PathGroupId>(group_raw, "path group"));
    path.group = group;
    PATHOBS_TRY(generation_of_path, optional_u64(item, "generation", generation.id.value()));
    path.generation = RouteGenerationId::from_value(generation_of_path);
    PATHOBS_TRY(hops, require_member(item, "hops"));
    if (!hops->is_array()) {
      return Error{ErrorCode::InvalidFormat, "path hops must be an array"};
    }
    if (hops->items().size() > limits.max_hops_per_path) {
      return Error{ErrorCode::CapacityExceeded, "path exceeds the configured hop bound"};
    }
    std::uint32_t expected_index = 0;
    for (const auto& hop_item : hops->items()) {
      ExpectedHop hop;
      PATHOBS_TRY(hop_raw, require_u64(hop_item, "id"));
      PATHOBS_TRY(hop_id, to_id<HopId>(hop_raw, "hop"));
      hop.id = hop_id;
      PATHOBS_TRY(index, optional_u64(hop_item, "index", expected_index));
      hop.index = static_cast<std::uint32_t>(index);
      PATHOBS_TRY(device_raw, require_u64(hop_item, "device"));
      PATHOBS_TRY(device, to_id<DeviceId>(device_raw, "hop device"));
      hop.device = device;
      PATHOBS_TRY(ingress_raw, require_u64(hop_item, "ingress"));
      PATHOBS_TRY(ingress, to_id<PortId>(ingress_raw, "hop ingress port"));
      hop.ingress = ingress;
      PATHOBS_TRY(egress_raw, require_u64(hop_item, "egress"));
      PATHOBS_TRY(egress, to_id<PortId>(egress_raw, "hop egress port"));
      hop.egress = egress;
      PATHOBS_TRY(link_raw, optional_u64(hop_item, "link", 0));
      if (link_raw != 0) {
        hop.egress_link = LinkId::from_value(link_raw);
        hop.has_egress_link = true;
      }
      path.hops.push_back(hop);
      ++expected_index;
    }
    generation.paths.push_back(std::move(path));
  }

  const Status finalized = generation.finalize(limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  return generation;
}

Result<ObservedPathEvidence> parse_evidence(std::string_view text, const Limits& limits) {
  PATHOBS_TRY(document, JsonValue::parse(text, limits));
  const Status kind_ok = check_kind(document, kEvidenceKindTag);
  if (!kind_ok.has_value()) {
    return kind_ok.error();
  }

  ObservedPathEvidence evidence;
  PATHOBS_TRY(source_raw, require_u64(document, "source"));
  PATHOBS_TRY(source, to_id<SourceId>(source_raw, "source"));
  evidence.source = source;
  PATHOBS_TRY(incarnation_raw, require_u64(document, "incarnation"));
  PATHOBS_TRY(incarnation, to_id<SourceIncarnationId>(incarnation_raw, "source incarnation"));
  evidence.incarnation = incarnation;
  PATHOBS_TRY(sequence_raw, require_u64(document, "sequence"));
  PATHOBS_TRY(sequence, to_id<SourceSequence>(sequence_raw, "sequence"));
  evidence.sequence = sequence;

  PATHOBS_TRY(surface_text, require_text(document, "surface"));
  PATHOBS_TRY(surface, parse_proof_surface(surface_text));
  evidence.surface = surface;
  PATHOBS_TRY(authority_text, require_text(document, "authority"));
  PATHOBS_TRY(authority, parse_authority_class(authority_text));
  evidence.authority = authority;

  PATHOBS_TRY(epoch_raw, require_u64(document, "epoch"));
  PATHOBS_TRY(epoch, to_id<EpochId>(epoch_raw, "epoch"));
  evidence.epoch = epoch;
  PATHOBS_TRY(claimed_raw, require_u64(document, "claimed_incarnation"));
  PATHOBS_TRY(claimed, to_id<IncarnationId>(claimed_raw, "claimed incarnation"));
  evidence.claimed_incarnation = claimed;
  PATHOBS_TRY(revision_raw, require_u64(document, "revision"));
  PATHOBS_TRY(revision, to_id<RevisionId>(revision_raw, "revision"));
  evidence.revision = revision;

  PATHOBS_TRY(route_generation_raw, optional_u64(document, "route_generation", 0));
  if (route_generation_raw != 0) {
    evidence.route_generation = MaybeId<RouteGenerationId>(RouteGenerationId::from_value(route_generation_raw));
  }
  PATHOBS_TRY(topology_generation_raw, optional_u64(document, "topology_generation", 0));
  if (topology_generation_raw != 0) {
    evidence.topology_generation =
        MaybeId<TopologyGenerationId>(TopologyGenerationId::from_value(topology_generation_raw));
  }
  PATHOBS_TRY(path_raw, optional_u64(document, "path", 0));
  if (path_raw != 0) {
    evidence.path = MaybeId<PathId>(PathId::from_value(path_raw));
  }
  PATHOBS_TRY(group_raw, optional_u64(document, "group", 0));
  if (group_raw != 0) {
    evidence.group = MaybeId<PathGroupId>(PathGroupId::from_value(group_raw));
  }
  PATHOBS_TRY(origin, optional_device(document, "origin"));
  evidence.origin = origin;
  PATHOBS_TRY(destination, optional_device(document, "destination"));
  evidence.destination = destination;

  PATHOBS_TRY(observed_at, optional_time(document, "observed_at", TimePoint::unset()));
  evidence.observed_at = observed_at;
  PATHOBS_TRY(received_at, optional_time(document, "received_at", observed_at));
  evidence.received_at = received_at;

  PATHOBS_TRY(truncated, optional_boolean(document, "truncated", false));
  evidence.source_truncated = truncated;
  PATHOBS_TRY(completeness_text, optional_text(document, "completeness", std::string("unknown")));
  PATHOBS_TRY(completeness, parse_evidence_completeness(completeness_text));
  evidence.declared_completeness = completeness;
  PATHOBS_TRY(payload_hex, optional_text(document, "payload_digest", std::string{}));
  if (!payload_hex.empty()) {
    PATHOBS_TRY(payload, Digest::parse_hex(payload_hex));
    evidence.payload_digest = payload;
  }
  PATHOBS_TRY(note, optional_text(document, "note", std::string{}));
  evidence.note = std::move(note);

  const JsonValue* hops = optional_member(document, "hops");
  if (hops != nullptr) {
    if (!hops->is_array()) {
      return Error{ErrorCode::InvalidFormat, "hops must be an array"};
    }
    if (hops->items().size() > limits.max_hops_per_path) {
      return Error{ErrorCode::CapacityExceeded, "hop list exceeds the configured bound"};
    }
    std::uint32_t previous = 0;
    bool first = true;
    for (const auto& item : hops->items()) {
      ObservedHop hop;
      PATHOBS_TRY(index, optional_u64(item, "index", first ? 0 : previous + 1));
      hop.index = static_cast<std::uint32_t>(index);
      PATHOBS_TRY(device_raw, require_u64(item, "device"));
      PATHOBS_TRY(device, to_id<DeviceId>(device_raw, "hop device"));
      hop.device = device;
      PATHOBS_TRY(ingress_raw, optional_u64(item, "ingress", 0));
      hop.ingress = PortId::from_value(ingress_raw);
      PATHOBS_TRY(egress_raw, optional_u64(item, "egress", 0));
      hop.egress = PortId::from_value(egress_raw);
      PATHOBS_TRY(link_raw, optional_u64(item, "link", 0));
      PATHOBS_TRY(provenance_text, optional_text(item, "link_provenance", std::string("absent")));
      PATHOBS_TRY(provenance, parse_hop_link_provenance(provenance_text));
      hop.link = LinkId::from_value(link_raw);
      hop.link_provenance = provenance;
      PATHOBS_TRY(at, optional_time(item, "at", TimePoint::unset()));
      hop.at = at;
      evidence.hops.push_back(hop);
      previous = hop.index;
      first = false;
    }
  }

  const Status valid = evidence.validate(limits);
  if (!valid.has_value()) {
    return valid.error();
  }
  evidence.seal();
  return evidence;
}

Result<std::vector<ObservedPathEvidence>> parse_evidence_lines(std::string_view text,
                                                               const Limits& limits) {
  std::vector<ObservedPathEvidence> out;
  std::size_t position = 0;
  std::size_t line_number = 0;
  while (position < text.size()) {
    std::size_t end = text.find(0x0A, position);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(position, end - position);
    position = end + 1;
    ++line_number;
    while (!line.empty() && (line.back() == 0x0D || line.back() == ' ' || line.back() == 0x09)) {
      line.remove_suffix(1);
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == 0x09)) {
      ++start;
    }
    line.remove_prefix(start);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    Result<ObservedPathEvidence> parsed = parse_evidence(line, limits);
    if (!parsed.has_value()) {
      return Error{parsed.error().code,
                   "evidence line " + std::to_string(line_number) + ": " + parsed.error().message,
                   parsed.error().detail};
    }
    out.push_back(std::move(parsed).value());
    if (out.size() > limits.max_evidence_per_ingest_batch) {
      return Error{ErrorCode::CapacityExceeded, "evidence document exceeds the ingest bound"};
    }
  }
  return out;
}

std::string to_json(const SourceDescriptor& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", std::string(kSourceKindTag));
  set_u64(object, "id", value.id.value());
  set_string(object, "name", value.name);
  set_string(object, "source_kind", to_string(value.kind));
  set_string(object, "surface", to_string(value.surface));
  set_string(object, "authority", to_string(value.authority));
  JsonValue capabilities = JsonValue::make_array();
  if (value.capabilities.expected_state) {
    capabilities.push_back(JsonValue::make_string("expected_state"));
  }
  if (value.capabilities.observed_evidence) {
    capabilities.push_back(JsonValue::make_string("observed_evidence"));
  }
  if (value.capabilities.topology) {
    capabilities.push_back(JsonValue::make_string("topology"));
  }
  object.set("capabilities", std::move(capabilities));
  set_u64(object, "max_observation_age_ms",
          static_cast<std::uint64_t>(value.max_observation_age.count() / 1000000));
  set_u64(object, "clock_skew_tolerance_ms",
          static_cast<std::uint64_t>(value.clock_skew_tolerance.count() / 1000000));
  return object.dump(pretty);
}

std::string to_json(const TopologyGeneration& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", std::string(kTopologyKindTag));
  set_u64(object, "generation", value.id.value());
  set_u64(object, "epoch", value.epoch.value());
  set_u64(object, "incarnation", value.incarnation.value());
  set_u64(object, "revision", value.revision.value());
  set_u64(object, "sequence", value.sequence.value());
  set_u64(object, "source", value.source.value());
  set_time(object, "created", value.created);
  set_time(object, "received", value.received);
  set_string(object, "digest", value.digest.hex());
  JsonValue devices = JsonValue::make_array();
  for (const auto& device : value.devices) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "id", device.id.value());
    set_string(item, "name", device.name);
    set_string(item, "role", to_string(device.role));
    devices.push_back(std::move(item));
  }
  object.set("devices", std::move(devices));
  JsonValue ports = JsonValue::make_array();
  for (const auto& port : value.ports) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "id", port.id.value());
    set_u64(item, "device", port.device.value());
    set_string(item, "name", port.name);
    set_u64(item, "index", port.index);
    ports.push_back(std::move(item));
  }
  object.set("ports", std::move(ports));
  JsonValue links = JsonValue::make_array();
  for (const auto& link : value.links) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "id", link.id.value());
    set_u64(item, "local_device", link.local_device.value());
    set_u64(item, "local_port", link.local_port.value());
    set_u64(item, "remote_device", link.remote_device.value());
    set_u64(item, "remote_port", link.remote_port.value());
    set_string(item, "kind", to_string(link.kind));
    links.push_back(std::move(item));
  }
  object.set("links", std::move(links));
  return object.dump(pretty);
}

std::string to_json(const RouteGeneration& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", std::string(kRoutesKindTag));
  set_u64(object, "generation", value.id.value());
  set_u64(object, "epoch", value.epoch.value());
  set_u64(object, "incarnation", value.incarnation.value());
  set_u64(object, "revision", value.revision.value());
  set_u64(object, "sequence", value.sequence.value());
  set_u64(object, "source", value.source.value());
  set_u64(object, "topology_generation", value.topology.value());
  set_time(object, "created", value.created);
  set_time(object, "received", value.received);
  set_string(object, "digest", value.digest.hex());
  JsonValue groups = JsonValue::make_array();
  for (const auto& group : value.groups) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "id", group.id.value());
    set_u64(item, "origin", group.origin.value());
    set_u64(item, "destination", group.destination.value());
    set_string(item, "label", group.label);
    JsonValue members = JsonValue::make_array();
    for (const auto member : group.members) {
      members.push_back(JsonValue::make_number(member.value()));
    }
    item.set("members", std::move(members));
    groups.push_back(std::move(item));
  }
  object.set("groups", std::move(groups));
  JsonValue paths = JsonValue::make_array();
  for (const auto& path : value.paths) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "id", path.id.value());
    set_u64(item, "group", path.group.value());
    set_u64(item, "generation", path.generation.value());
    JsonValue hops = JsonValue::make_array();
    for (const auto& hop : path.hops) {
      hops.push_back(hop_json(hop));
    }
    item.set("hops", std::move(hops));
    paths.push_back(std::move(item));
  }
  object.set("paths", std::move(paths));
  return object.dump(pretty);
}

std::string to_json(const ObservedPathEvidence& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", std::string(kEvidenceKindTag));
  set_string(object, "id", value.id.str());
  set_u64(object, "source", value.source.value());
  set_u64(object, "incarnation", value.incarnation.value());
  set_u64(object, "sequence", value.sequence.value());
  set_string(object, "surface", to_string(value.surface));
  set_string(object, "authority", to_string(value.authority));
  set_u64(object, "epoch", value.epoch.value());
  set_u64(object, "claimed_incarnation", value.claimed_incarnation.value());
  set_u64(object, "revision", value.revision.value());
  if (value.route_generation.present()) {
    set_u64(object, "route_generation", value.route_generation.value().value());
  }
  if (value.topology_generation.present()) {
    set_u64(object, "topology_generation", value.topology_generation.value().value());
  }
  if (value.path.present()) {
    set_u64(object, "path", value.path.value().value());
  }
  if (value.group.present()) {
    set_u64(object, "group", value.group.value().value());
  }
  if (value.origin.present()) {
    set_u64(object, "origin", value.origin.value().value());
  }
  if (value.destination.present()) {
    set_u64(object, "destination", value.destination.value().value());
  }
  set_time(object, "observed_at", value.observed_at);
  set_time(object, "received_at", value.received_at);
  set_bool(object, "truncated", value.source_truncated);
  set_string(object, "completeness", to_string(value.declared_completeness));
  set_string(object, "payload_digest", value.payload_digest.hex());
  set_string(object, "note", value.note);
  JsonValue hops = JsonValue::make_array();
  for (const auto& hop : value.hops) {
    hops.push_back(observed_hop_json(hop));
  }
  object.set("hops", std::move(hops));
  return object.dump(pretty);
}

std::string to_json(const ComparisonResult& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.comparison");
  set_string(object, "id", value.id.str());
  set_u64(object, "route_generation", value.generation.value());
  set_u64(object, "topology_generation", value.topology.value());
  if (value.group.present()) {
    set_u64(object, "group", value.group.value().value());
  }
  if (value.path.present()) {
    set_u64(object, "path", value.path.value().value());
  }
  if (value.matched_path.present()) {
    set_u64(object, "matched_path", value.matched_path.value().value());
  }
  set_string(object, "outcome", to_string(value.outcome));
  set_string(object, "compliance", value.is_compliance() ? "true" : "false");
  set_string(object, "primary_class", to_code(value.primary));
  set_string(object, "primary_category", to_string(category_of(value.primary)));
  JsonValue classes = JsonValue::make_array();
  for (const auto entry : value.classes) {
    classes.push_back(JsonValue::make_string(to_code(entry)));
  }
  object.set("classes", std::move(classes));
  set_string(object, "freshness", to_string(value.freshness));
  set_string(object, "completeness", to_string(value.completeness));
  set_string(object, "consistency", to_string(value.consistency));
  set_string(object, "proof_surface", to_string(value.surface));
  set_bool(object, "multipath", value.multipath);
  set_bool(object, "multipath_ambiguous", value.multipath_ambiguous);
  set_u64(object, "evidence_offered", value.evidence_offered);
  set_u64(object, "evidence_considered", value.evidence_considered);
  set_u64(object, "evidence_rejected", value.evidence_rejected);
  set_u64(object, "duplicate_evidence", value.duplicate_evidence);
  set_time(object, "evaluated_at", value.evaluated_at);
  JsonValue judgements = JsonValue::make_array();
  for (const auto& judgement : value.judgements) {
    JsonValue item = JsonValue::make_object();
    set_string(item, "evidence", judgement.id.str());
    set_bool(item, "accepted", judgement.accepted);
    set_string(item, "rejection", to_code(judgement.rejection));
    set_string(item, "freshness", to_string(judgement.freshness));
    set_string(item, "surface", to_string(judgement.surface));
    set_string(item, "authority", to_string(judgement.authority));
    set_string(item, "completeness", to_string(judgement.completeness));
    set_string(item, "reason", judgement.reason);
    judgements.push_back(std::move(item));
  }
  object.set("judgements", std::move(judgements));
  JsonValue differences = JsonValue::make_array();
  for (const auto& difference : value.differences) {
    JsonValue item = JsonValue::make_object();
    set_u64(item, "index", difference.index);
    set_string(item, "detail", to_code(difference.detail));
    if (difference.has_expected) {
      set_u64(item, "expected_device", difference.expected.device.value());
      set_u64(item, "expected_ingress", difference.expected.ingress.value());
      set_u64(item, "expected_egress", difference.expected.egress.value());
    }
    if (difference.has_observed) {
      set_u64(item, "observed_device", difference.observed.device.value());
      if (difference.observed.ingress.valid()) {
        set_u64(item, "observed_ingress", difference.observed.ingress.value());
      }
      if (difference.observed.egress.valid()) {
        set_u64(item, "observed_egress", difference.observed.egress.value());
      }
    }
    differences.push_back(std::move(item));
  }
  object.set("differences", std::move(differences));
  JsonValue rationale = JsonValue::make_array();
  for (const auto& line : value.rationale) {
    rationale.push_back(JsonValue::make_string(line));
  }
  object.set("rationale", std::move(rationale));
  return object.dump(pretty);
}

std::string to_json(const Finding& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.finding");
  set_string(object, "id", value.id.str());
  set_string(object, "class", to_code(value.cls));
  set_string(object, "category", to_string(category_of(value.cls)));
  if (value.group.present()) {
    set_u64(object, "group", value.group.value().value());
  }
  if (value.path.present()) {
    set_u64(object, "path", value.path.value().value());
  }
  set_u64(object, "route_generation", value.generation.value());
  set_u64(object, "topology_generation", value.topology.value());
  set_string(object, "status", to_string(value.status));
  set_u64(object, "occurrences", value.occurrences);
  set_time(object, "first_seen", value.first_seen);
  set_time(object, "last_seen", value.last_seen);
  set_string(object, "last_comparison", value.last_comparison.str());
  set_string(object, "detail", value.detail);
  JsonValue evidence = JsonValue::make_array();
  for (const auto& item : value.evidence) {
    evidence.push_back(JsonValue::make_string(item.str()));
  }
  object.set("evidence", std::move(evidence));
  JsonValue hops = JsonValue::make_array();
  for (const auto hop : value.hop_indices) {
    hops.push_back(JsonValue::make_number(static_cast<std::uint64_t>(hop)));
  }
  object.set("hop_indices", std::move(hops));
  return object.dump(pretty);
}

std::string to_json(const HistoryRecord& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.history_record");
  set_string(object, "id", value.id.str());
  set_u64(object, "route_generation", value.generation.value());
  set_u64(object, "topology_generation", value.topology.value());
  if (value.group.present()) {
    set_u64(object, "group", value.group.value().value());
  }
  if (value.path.present()) {
    set_u64(object, "path", value.path.value().value());
  }
  set_string(object, "outcome", to_string(value.outcome));
  set_string(object, "primary_class", to_code(value.primary));
  set_string(object, "freshness", to_string(value.freshness));
  set_string(object, "completeness", to_string(value.completeness));
  set_string(object, "consistency", to_string(value.consistency));
  set_string(object, "proof_surface", to_string(value.surface));
  set_u64(object, "evidence_considered", value.evidence_considered);
  set_u64(object, "evidence_rejected", value.evidence_rejected);
  set_time(object, "evaluated_at", value.evaluated_at);
  return object.dump(pretty);
}

std::string to_json(const HistoryQueryResult& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.history");
  set_u64(object, "matched", value.matched);
  set_u64(object, "excluded_by_window", value.excluded_by_window);
  set_u64(object, "excluded_by_filter", value.excluded_by_filter);
  set_bool(object, "truncated", value.truncated);
  JsonValue records = JsonValue::make_array();
  for (const auto& record : value.records) {
    Result<JsonValue> encoded = JsonValue::parse(to_json(record, false), default_limits());
    records.push_back(encoded.has_value() ? std::move(encoded).value() : JsonValue::make_null());
  }
  object.set("records", std::move(records));
  return object.dump(pretty);
}

std::string to_json(const RecoveryReport& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.store_recovery");
  set_bool(object, "created", value.created);
  set_bool(object, "used_secondary_header_slot", value.used_secondary_header_slot);
  set_bool(object, "unclean_previous_shutdown", value.unclean_previous_shutdown);
  set_bool(object, "corrupt_tail_truncated", value.corrupt_tail_truncated);
  set_u64(object, "valid_records", value.valid_records);
  set_u64(object, "invalid_records", value.invalid_records);
  set_u64(object, "bytes", value.bytes);
  set_u64(object, "restart_count", value.restart_count);
  set_u64(object, "header_generation", value.header_generation);
  set_u64(object, "store_incarnation", value.incarnation.value());
  set_string(object, "truncate_detail", value.truncate_detail);
  set_time(object, "created_at", value.created_at);
  set_time(object, "opened_at", value.opened_at);
  return object.dump(pretty);
}

std::string to_json(const MetricsSnapshot& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.metrics");
  const auto add = [&object](const char* key, std::uint64_t count) {
    object.set(key, JsonValue::make_number(count));
  };
  add("evidence_offered", value.evidence_offered);
  add("evidence_accepted", value.evidence_accepted);
  add("evidence_rejected", value.evidence_rejected);
  add("evidence_unregistered_source", value.evidence_unregistered_source);
  add("evidence_replayed", value.evidence_replayed);
  add("evidence_gap_events", value.evidence_gap_events);
  add("comparisons", value.comparisons);
  add("matches", value.matches);
  add("divergences", value.divergences);
  add("indeterminate", value.indeterminate);
  add("unsupported", value.unsupported);
  add("no_expected_state", value.no_expected_state);
  add("findings_recorded", value.findings_recorded);
  add("findings_resolved", value.findings_resolved);
  add("findings_evicted", value.findings_evicted);
  add("queue_submitted", value.queue_submitted);
  add("queue_rejected", value.queue_rejected);
  add("tasks_completed", value.tasks_completed);
  add("tasks_cancelled", value.tasks_cancelled);
  add("store_appends", value.store_appends);
  add("store_records_recovered", value.store_records_recovered);
  add("store_corrupt_tail_truncations", value.store_corrupt_tail_truncations);
  add("transport_frames_in", value.transport_frames_in);
  add("transport_frames_out", value.transport_frames_out);
  add("transport_bytes_in", value.transport_bytes_in);
  add("transport_bytes_out", value.transport_bytes_out);
  add("transport_protocol_violations", value.transport_protocol_violations);
  return object.dump(pretty);
}

std::string to_json(const RuntimeStats& value, bool pretty) {
  JsonValue object = JsonValue::make_object();
  set_string(object, "kind", "pathobs.runtime_stats");
  set_string(object, "state", to_string(value.state));
  set_bool(object, "has_route_generation", value.has_route_generation);
  set_bool(object, "has_topology_generation", value.has_topology_generation);
  set_u64(object, "epoch", value.epoch.value());
  set_u64(object, "incarnation", value.incarnation.value());
  set_u64(object, "route_generation", value.route_generation.value());
  set_u64(object, "topology_generation", value.topology_generation.value());
  set_u64(object, "route_revision", value.route_revision.value());
  set_u64(object, "evidence_buckets", value.evidence_buckets);
  set_u64(object, "evidence_records", value.evidence_records);
  set_u64(object, "findings", value.findings);
  set_u64(object, "findings_open", value.findings_open);
  set_u64(object, "history", value.history);
  set_u64(object, "queue_pending", value.queue_pending);
  Result<JsonValue> metrics = JsonValue::parse(to_json(value.metrics, false), default_limits());
  object.set("metrics", metrics.has_value() ? std::move(metrics).value() : JsonValue::make_null());
  Result<JsonValue> recovery = JsonValue::parse(to_json(value.recovery, false), default_limits());
  object.set("recovery",
             recovery.has_value() ? std::move(recovery).value() : JsonValue::make_null());
  return object.dump(pretty);
}

} // namespace pathobs::documents
