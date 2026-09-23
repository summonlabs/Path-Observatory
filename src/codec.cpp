// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/codec.hpp"

namespace pathobs::codec {
namespace {

constexpr std::uint32_t kMaxSequenceElements = 4000000;

void write_maybe_path(CanonicalWriter& writer, const MaybeId<PathId>& value) {
  writer.boolean(value.present());
  writer.id(value.value());
}

void write_maybe_group(CanonicalWriter& writer, const MaybeId<PathGroupId>& value) {
  writer.boolean(value.present());
  writer.id(value.value());
}

void write_maybe_device(CanonicalWriter& writer, const MaybeId<DeviceId>& value) {
  writer.boolean(value.present());
  writer.id(value.value());
}

void write_maybe_route_generation(CanonicalWriter& writer,
                                  const MaybeId<RouteGenerationId>& value) {
  writer.boolean(value.present());
  writer.id(value.value());
}

void write_maybe_topology_generation(CanonicalWriter& writer,
                                     const MaybeId<TopologyGenerationId>& value) {
  writer.boolean(value.present());
  writer.id(value.value());
}

Result<bool> read_presence(CanonicalReader& reader) { return reader.boolean(); }

template <class Tag>
Result<MaybeId<StrongId<Tag>>> read_maybe_strong(CanonicalReader& reader) {
  PATHOBS_TRY(present, read_presence(reader));
  PATHOBS_TRY(raw, reader.u64());
  if (!present) {
    return MaybeId<StrongId<Tag>>{};
  }
  return MaybeId<StrongId<Tag>>(StrongId<Tag>::from_value(raw));
}

} // namespace

std::string encode(const SourceDescriptor& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.source");
  writer.id(value.id);
  writer.text(value.name);
  writer.u8(static_cast<std::uint8_t>(value.kind));
  writer.u8(static_cast<std::uint8_t>(value.surface));
  writer.u8(static_cast<std::uint8_t>(value.authority));
  writer.boolean(value.capabilities.expected_state);
  writer.boolean(value.capabilities.observed_evidence);
  writer.boolean(value.capabilities.topology);
  writer.i64(value.max_observation_age.count());
  writer.i64(value.clock_skew_tolerance.count());
  return writer.take();
}

Result<SourceDescriptor> decode_source_descriptor(CanonicalReader& reader, const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.source") {
    return Error{ErrorCode::InvalidFormat, "record is not a source descriptor"};
  }
  SourceDescriptor value;
  PATHOBS_TRY(source_id, reader.id<SourceIdTag>());
  value.id = source_id;
  PATHOBS_TRY(name, reader.text(limits.max_string_bytes));
  value.name = std::move(name);
  PATHOBS_TRY(kind, reader.u8());
  if (kind > static_cast<std::uint8_t>(SourceKind::Simulator)) {
    return Error{ErrorCode::InvalidFormat, "source kind is out of range"};
  }
  value.kind = static_cast<SourceKind>(kind);
  PATHOBS_TRY(surface, reader.u8());
  if (surface > static_cast<std::uint8_t>(ProofSurface::Unsupported)) {
    return Error{ErrorCode::InvalidFormat, "proof surface is out of range"};
  }
  value.surface = static_cast<ProofSurface>(surface);
  PATHOBS_TRY(authority, reader.u8());
  if (authority > static_cast<std::uint8_t>(AuthorityClass::Authoritative)) {
    return Error{ErrorCode::InvalidFormat, "authority class is out of range"};
  }
  value.authority = static_cast<AuthorityClass>(authority);
  PATHOBS_TRY(expected_state, reader.boolean());
  PATHOBS_TRY(observed, reader.boolean());
  PATHOBS_TRY(topology, reader.boolean());
  value.capabilities.expected_state = expected_state;
  value.capabilities.observed_evidence = observed;
  value.capabilities.topology = topology;
  PATHOBS_TRY(age, reader.i64());
  PATHOBS_TRY(skew, reader.i64());
  value.max_observation_age = Duration{age};
  value.clock_skew_tolerance = Duration{skew};
  const Status valid = value.validate();
  if (!valid.has_value()) {
    return valid.error();
  }
  return value;
}

std::string encode(const TopologyGeneration& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.topology");
  writer.id(value.id);
  writer.id(value.epoch);
  writer.id(value.incarnation);
  writer.id(value.revision);
  writer.id(value.sequence);
  writer.id(value.source);
  writer.i64(value.created.unix_nanos());
  writer.i64(value.received.unix_nanos());
  writer.count(value.devices.size());
  for (const auto& device : value.devices) {
    writer.id(device.id);
    writer.text(device.name);
    writer.u8(static_cast<std::uint8_t>(device.role));
  }
  writer.count(value.ports.size());
  for (const auto& port : value.ports) {
    writer.id(port.id);
    writer.id(port.device);
    writer.text(port.name);
    writer.u32(port.index);
  }
  writer.count(value.links.size());
  for (const auto& link : value.links) {
    writer.id(link.id);
    writer.id(link.local_device);
    writer.id(link.local_port);
    writer.id(link.remote_device);
    writer.id(link.remote_port);
    writer.u8(static_cast<std::uint8_t>(link.kind));
  }
  return writer.take();
}

Result<TopologyGeneration> decode_topology_generation(CanonicalReader& reader,
                                                      const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.topology") {
    return Error{ErrorCode::InvalidFormat, "record is not a topology generation"};
  }
  TopologyGeneration value;
  PATHOBS_TRY(id, reader.id<TopologyGenerationIdTag>());
  value.id = id;
  PATHOBS_TRY(epoch, reader.id<EpochIdTag>());
  value.epoch = epoch;
  PATHOBS_TRY(incarnation, reader.id<IncarnationIdTag>());
  value.incarnation = incarnation;
  PATHOBS_TRY(revision, reader.id<RevisionIdTag>());
  value.revision = revision;
  PATHOBS_TRY(sequence, reader.id<SourceSequenceTag>());
  value.sequence = sequence;
  PATHOBS_TRY(source, reader.id<SourceIdTag>());
  value.source = source;
  PATHOBS_TRY(created, reader.i64());
  PATHOBS_TRY(received, reader.i64());
  value.created = TimePoint::from_unix_nanos(created);
  value.received = TimePoint::from_unix_nanos(received);

  PATHOBS_TRY(device_count, reader.count(limits.max_devices_per_topology));
  value.devices.reserve(device_count);
  for (std::uint32_t i = 0; i < device_count; ++i) {
    DeviceRecord record;
    PATHOBS_TRY(device_id, reader.id<DeviceIdTag>());
    record.id = device_id;
    PATHOBS_TRY(name, reader.text(limits.max_string_bytes));
    record.name = std::move(name);
    PATHOBS_TRY(role, reader.u8());
    if (role > static_cast<std::uint8_t>(DeviceRole::Appliance)) {
      return Error{ErrorCode::InvalidFormat, "device role is out of range"};
    }
    record.role = static_cast<DeviceRole>(role);
    value.devices.push_back(std::move(record));
  }

  PATHOBS_TRY(port_count, reader.count(kMaxSequenceElements));
  value.ports.reserve(port_count);
  for (std::uint32_t i = 0; i < port_count; ++i) {
    PortRecord record;
    PATHOBS_TRY(port_id, reader.id<PortIdTag>());
    record.id = port_id;
    PATHOBS_TRY(device_id, reader.id<DeviceIdTag>());
    record.device = device_id;
    PATHOBS_TRY(name, reader.text(limits.max_string_bytes));
    record.name = std::move(name);
    PATHOBS_TRY(index, reader.u32());
    record.index = index;
    value.ports.push_back(std::move(record));
  }

  PATHOBS_TRY(link_count, reader.count(limits.max_links_per_topology));
  value.links.reserve(link_count);
  for (std::uint32_t i = 0; i < link_count; ++i) {
    LinkRecord record;
    PATHOBS_TRY(link_id, reader.id<LinkIdTag>());
    record.id = link_id;
    PATHOBS_TRY(local_device, reader.id<DeviceIdTag>());
    record.local_device = local_device;
    PATHOBS_TRY(local_port, reader.id<PortIdTag>());
    record.local_port = local_port;
    PATHOBS_TRY(remote_device, reader.id<DeviceIdTag>());
    record.remote_device = remote_device;
    PATHOBS_TRY(remote_port, reader.id<PortIdTag>());
    record.remote_port = remote_port;
    PATHOBS_TRY(kind, reader.u8());
    if (kind > static_cast<std::uint8_t>(LinkKind::Lag)) {
      return Error{ErrorCode::InvalidFormat, "link kind is out of range"};
    }
    record.kind = static_cast<LinkKind>(kind);
    value.links.push_back(std::move(record));
  }

  const Status finalized = value.finalize(limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  return value;
}

std::string encode(const RouteGeneration& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.routes");
  writer.id(value.id);
  writer.id(value.epoch);
  writer.id(value.incarnation);
  writer.id(value.revision);
  writer.id(value.sequence);
  writer.id(value.source);
  writer.id(value.topology);
  writer.i64(value.created.unix_nanos());
  writer.i64(value.received.unix_nanos());
  writer.count(value.groups.size());
  for (const auto& group : value.groups) {
    writer.id(group.id);
    writer.id(group.origin);
    writer.id(group.destination);
    writer.text(group.label);
    writer.count(group.members.size());
    for (const auto member : group.members) {
      writer.id(member);
    }
  }
  writer.count(value.paths.size());
  for (const auto& path : value.paths) {
    writer.id(path.id);
    writer.id(path.group);
    writer.id(path.generation);
    writer.count(path.hops.size());
    for (const auto& hop : path.hops) {
      writer.id(hop.id);
      writer.u32(hop.index);
      writer.id(hop.device);
      writer.id(hop.ingress);
      writer.id(hop.egress);
      writer.boolean(hop.has_egress_link);
      writer.id(hop.egress_link);
    }
  }
  return writer.take();
}

Result<RouteGeneration> decode_route_generation(CanonicalReader& reader, const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.routes") {
    return Error{ErrorCode::InvalidFormat, "record is not a route generation"};
  }
  RouteGeneration value;
  PATHOBS_TRY(id, reader.id<RouteGenerationIdTag>());
  value.id = id;
  PATHOBS_TRY(epoch, reader.id<EpochIdTag>());
  value.epoch = epoch;
  PATHOBS_TRY(incarnation, reader.id<IncarnationIdTag>());
  value.incarnation = incarnation;
  PATHOBS_TRY(revision, reader.id<RevisionIdTag>());
  value.revision = revision;
  PATHOBS_TRY(sequence, reader.id<SourceSequenceTag>());
  value.sequence = sequence;
  PATHOBS_TRY(source, reader.id<SourceIdTag>());
  value.source = source;
  PATHOBS_TRY(topology, reader.id<TopologyGenerationIdTag>());
  value.topology = topology;
  PATHOBS_TRY(created, reader.i64());
  PATHOBS_TRY(received, reader.i64());
  value.created = TimePoint::from_unix_nanos(created);
  value.received = TimePoint::from_unix_nanos(received);

  PATHOBS_TRY(group_count, reader.count(limits.max_groups_per_generation));
  value.groups.reserve(group_count);
  for (std::uint32_t i = 0; i < group_count; ++i) {
    PathGroup group;
    PATHOBS_TRY(group_id, reader.id<PathGroupIdTag>());
    group.id = group_id;
    PATHOBS_TRY(origin, reader.id<DeviceIdTag>());
    group.origin = origin;
    PATHOBS_TRY(destination, reader.id<DeviceIdTag>());
    group.destination = destination;
    PATHOBS_TRY(label, reader.text(limits.max_string_bytes));
    group.label = std::move(label);
    PATHOBS_TRY(member_count, reader.count(limits.max_members_per_group));
    group.members.reserve(member_count);
    for (std::uint32_t m = 0; m < member_count; ++m) {
      PATHOBS_TRY(member, reader.id<PathIdTag>());
      group.members.push_back(member);
    }
    value.groups.push_back(std::move(group));
  }

  PATHOBS_TRY(path_count, reader.count(limits.max_paths_per_generation));
  value.paths.reserve(path_count);
  for (std::uint32_t i = 0; i < path_count; ++i) {
    ExpectedPath path;
    PATHOBS_TRY(path_id, reader.id<PathIdTag>());
    path.id = path_id;
    PATHOBS_TRY(group_id, reader.id<PathGroupIdTag>());
    path.group = group_id;
    PATHOBS_TRY(generation, reader.id<RouteGenerationIdTag>());
    path.generation = generation;
    PATHOBS_TRY(hop_count, reader.count(limits.max_hops_per_path));
    path.hops.reserve(hop_count);
    for (std::uint32_t h = 0; h < hop_count; ++h) {
      ExpectedHop hop;
      PATHOBS_TRY(hop_id, reader.id<HopIdTag>());
      hop.id = hop_id;
      PATHOBS_TRY(index, reader.u32());
      hop.index = index;
      PATHOBS_TRY(device, reader.id<DeviceIdTag>());
      hop.device = device;
      PATHOBS_TRY(ingress, reader.id<PortIdTag>());
      hop.ingress = ingress;
      PATHOBS_TRY(egress, reader.id<PortIdTag>());
      hop.egress = egress;
      PATHOBS_TRY(has_link, reader.boolean());
      hop.has_egress_link = has_link;
      PATHOBS_TRY(link, reader.id<LinkIdTag>());
      hop.egress_link = link;
      path.hops.push_back(hop);
    }
    value.paths.push_back(std::move(path));
  }

  const Status finalized = value.finalize(limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  return value;
}

std::string encode(const ObservedPathEvidence& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.evidence");
  writer.digest_id(value.id);
  writer.id(value.source);
  writer.id(value.incarnation);
  writer.id(value.sequence);
  writer.u8(static_cast<std::uint8_t>(value.surface));
  writer.u8(static_cast<std::uint8_t>(value.authority));
  write_maybe_path(writer, value.path);
  write_maybe_group(writer, value.group);
  write_maybe_device(writer, value.origin);
  write_maybe_device(writer, value.destination);
  writer.id(value.epoch);
  writer.id(value.claimed_incarnation);
  write_maybe_route_generation(writer, value.route_generation);
  write_maybe_topology_generation(writer, value.topology_generation);
  writer.id(value.revision);
  writer.i64(value.observed_at.unix_nanos());
  writer.i64(value.received_at.unix_nanos());
  writer.boolean(value.source_truncated);
  writer.u8(static_cast<std::uint8_t>(value.declared_completeness));
  writer.digest(value.payload_digest);
  writer.text(value.note);
  writer.count(value.hops.size());
  for (const auto& hop : value.hops) {
    writer.u32(hop.index);
    writer.id(hop.device);
    writer.id(hop.ingress);
    writer.id(hop.egress);
    writer.id(hop.link);
    writer.u8(static_cast<std::uint8_t>(hop.link_provenance));
    writer.i64(hop.at.unix_nanos());
  }
  return writer.take();
}

Result<ObservedPathEvidence> decode_evidence(CanonicalReader& reader, const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.evidence") {
    return Error{ErrorCode::InvalidFormat, "record is not an evidence record"};
  }
  ObservedPathEvidence value;
  PATHOBS_TRY(stored_id, reader.digest_id<EvidenceIdTag>());
  PATHOBS_TRY(source, reader.id<SourceIdTag>());
  value.source = source;
  PATHOBS_TRY(incarnation, reader.id<SourceIncarnationIdTag>());
  value.incarnation = incarnation;
  PATHOBS_TRY(sequence, reader.id<SourceSequenceTag>());
  value.sequence = sequence;
  PATHOBS_TRY(surface, reader.u8());
  if (surface > static_cast<std::uint8_t>(ProofSurface::Unsupported)) {
    return Error{ErrorCode::InvalidFormat, "proof surface is out of range"};
  }
  value.surface = static_cast<ProofSurface>(surface);
  PATHOBS_TRY(authority, reader.u8());
  if (authority > static_cast<std::uint8_t>(AuthorityClass::Authoritative)) {
    return Error{ErrorCode::InvalidFormat, "authority class is out of range"};
  }
  value.authority = static_cast<AuthorityClass>(authority);

  PATHOBS_TRY(path, read_maybe_strong<PathIdTag>(reader));
  value.path = path;
  PATHOBS_TRY(group, read_maybe_strong<PathGroupIdTag>(reader));
  value.group = group;
  PATHOBS_TRY(origin, read_maybe_strong<DeviceIdTag>(reader));
  value.origin = origin;
  PATHOBS_TRY(destination, read_maybe_strong<DeviceIdTag>(reader));
  value.destination = destination;

  PATHOBS_TRY(epoch, reader.id<EpochIdTag>());
  value.epoch = epoch;
  PATHOBS_TRY(claimed, reader.id<IncarnationIdTag>());
  value.claimed_incarnation = claimed;
  PATHOBS_TRY(route_generation, read_maybe_strong<RouteGenerationIdTag>(reader));
  value.route_generation = route_generation;
  PATHOBS_TRY(topology_generation, read_maybe_strong<TopologyGenerationIdTag>(reader));
  value.topology_generation = topology_generation;
  PATHOBS_TRY(revision, reader.id<RevisionIdTag>());
  value.revision = revision;

  PATHOBS_TRY(observed_at, reader.i64());
  PATHOBS_TRY(received_at, reader.i64());
  value.observed_at = TimePoint::from_unix_nanos(observed_at);
  value.received_at = TimePoint::from_unix_nanos(received_at);
  PATHOBS_TRY(truncated, reader.boolean());
  value.source_truncated = truncated;
  PATHOBS_TRY(completeness, reader.u8());
  if (completeness > static_cast<std::uint8_t>(EvidenceCompleteness::Complete)) {
    return Error{ErrorCode::InvalidFormat, "evidence completeness is out of range"};
  }
  value.declared_completeness = static_cast<EvidenceCompleteness>(completeness);
  PATHOBS_TRY(payload_digest, reader.digest());
  value.payload_digest = payload_digest;
  PATHOBS_TRY(note, reader.text(4096));
  value.note = std::move(note);

  PATHOBS_TRY(hop_count, reader.count(limits.max_hops_per_path));
  value.hops.reserve(hop_count);
  for (std::uint32_t i = 0; i < hop_count; ++i) {
    ObservedHop hop;
    PATHOBS_TRY(index, reader.u32());
    hop.index = index;
    PATHOBS_TRY(device, reader.id<DeviceIdTag>());
    hop.device = device;
    PATHOBS_TRY(ingress, reader.id<PortIdTag>());
    hop.ingress = ingress;
    PATHOBS_TRY(egress, reader.id<PortIdTag>());
    hop.egress = egress;
    PATHOBS_TRY(link, reader.id<LinkIdTag>());
    hop.link = link;
    PATHOBS_TRY(provenance, reader.u8());
    if (provenance > static_cast<std::uint8_t>(HopLinkProvenance::Correlated)) {
      return Error{ErrorCode::InvalidFormat, "hop link provenance is out of range"};
    }
    hop.link_provenance = static_cast<HopLinkProvenance>(provenance);
    PATHOBS_TRY(at, reader.i64());
    hop.at = TimePoint::from_unix_nanos(at);
    value.hops.push_back(hop);
  }

  const Status valid = value.validate(limits);
  if (!valid.has_value()) {
    return valid.error();
  }
  const EvidenceId recomputed = compute_evidence_id(value);
  if (recomputed != stored_id) {
    return Error{ErrorCode::IntegrityFailure,
                 "evidence identity does not match its content",
                 "stored " + stored_id.str() + " recomputed " + recomputed.str()};
  }
  value.id = stored_id;
  return value;
}

std::string encode(const ComparisonResult& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.comparison");
  writer.digest_id(value.id);
  value.write_canonical(writer);
  return writer.take();
}

Result<ComparisonResult> decode_comparison(CanonicalReader& reader, const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.comparison") {
    return Error{ErrorCode::InvalidFormat, "record is not a comparison"};
  }
  PATHOBS_TRY(stored_id, reader.digest_id<ComparisonIdTag>());
  ComparisonResult value;
  // The canonical body starts with its own domain tag, which is what separates
  // a comparison identity from any other identity computed over similar bytes.
  // The decoder must consume and verify it; skipping it misaligns every field
  // that follows.
  PATHOBS_TRY(body_tag, reader.text(64));
  if (body_tag != "pathobs.comparison") {
    return Error{ErrorCode::InvalidFormat, "comparison body tag does not match", body_tag};
  }
  PATHOBS_TRY(generation, reader.id<RouteGenerationIdTag>());
  value.generation = generation;
  PATHOBS_TRY(topology, reader.id<TopologyGenerationIdTag>());
  value.topology = topology;
  PATHOBS_TRY(group_present, reader.boolean());
  PATHOBS_TRY(group, reader.id<PathGroupIdTag>());
  if (group_present) {
    value.group = MaybeId<PathGroupId>(group);
  }
  PATHOBS_TRY(path_present, reader.boolean());
  PATHOBS_TRY(path, reader.id<PathIdTag>());
  if (path_present) {
    value.path = MaybeId<PathId>(path);
  }
  PATHOBS_TRY(matched_present, reader.boolean());
  PATHOBS_TRY(matched, reader.id<PathIdTag>());
  if (matched_present) {
    value.matched_path = MaybeId<PathId>(matched);
  }
  PATHOBS_TRY(outcome, reader.u8());
  if (outcome > static_cast<std::uint8_t>(MatchOutcome::Unsupported)) {
    return Error{ErrorCode::InvalidFormat, "match outcome is out of range"};
  }
  value.outcome = static_cast<MatchOutcome>(outcome);
  PATHOBS_TRY(primary, reader.u8());
  value.primary = static_cast<DivergenceClass>(primary);
  PATHOBS_TRY(class_count, reader.count(64));
  for (std::uint32_t i = 0; i < class_count; ++i) {
    PATHOBS_TRY(class_value, reader.u8());
    value.classes.push_back(static_cast<DivergenceClass>(class_value));
  }
  PATHOBS_TRY(freshness, reader.u8());
  value.freshness = static_cast<Freshness>(freshness);
  PATHOBS_TRY(completeness, reader.u8());
  value.completeness = static_cast<EvidenceCompleteness>(completeness);
  PATHOBS_TRY(consistency, reader.u8());
  value.consistency = static_cast<ObservationConsistency>(consistency);
  PATHOBS_TRY(surface, reader.u8());
  value.surface = static_cast<ProofSurface>(surface);
  PATHOBS_TRY(multipath, reader.boolean());
  value.multipath = multipath;
  PATHOBS_TRY(multipath_ambiguous, reader.boolean());
  value.multipath_ambiguous = multipath_ambiguous;
  // The submission counts are deliberately not part of the comparison identity
  // (see ComparisonResult::write_canonical), so they are not on the wire either.
  PATHOBS_TRY(considered, reader.u32());
  PATHOBS_TRY(rejected, reader.u32());
  value.evidence_considered = considered;
  value.evidence_rejected = rejected;

  PATHOBS_TRY(judgement_count, reader.count(limits.max_evidence_per_comparison));
  value.judgements.reserve(judgement_count);
  for (std::uint32_t i = 0; i < judgement_count; ++i) {
    EvidenceJudgement judgement;
    PATHOBS_TRY(judgement_id, reader.digest_id<EvidenceIdTag>());
    judgement.id = judgement_id;
    PATHOBS_TRY(accepted, reader.boolean());
    judgement.accepted = accepted;
    PATHOBS_TRY(rejection, reader.u8());
    judgement.rejection = static_cast<DivergenceClass>(rejection);
    PATHOBS_TRY(item_freshness, reader.u8());
    judgement.freshness = static_cast<Freshness>(item_freshness);
    PATHOBS_TRY(item_surface, reader.u8());
    judgement.surface = static_cast<ProofSurface>(item_surface);
    PATHOBS_TRY(item_authority, reader.u8());
    judgement.authority = static_cast<AuthorityClass>(item_authority);
    PATHOBS_TRY(item_completeness, reader.u8());
    judgement.completeness = static_cast<EvidenceCompleteness>(item_completeness);
    PATHOBS_TRY(item_class_count, reader.count(64));
    for (std::uint32_t c = 0; c < item_class_count; ++c) {
      PATHOBS_TRY(item_class, reader.u8());
      judgement.classes.push_back(static_cast<DivergenceClass>(item_class));
    }
    value.judgements.push_back(std::move(judgement));
  }

  PATHOBS_TRY(difference_count, reader.count(limits.max_hops_per_path));
  value.differences.reserve(difference_count);
  for (std::uint32_t i = 0; i < difference_count; ++i) {
    HopDifference difference;
    PATHOBS_TRY(index, reader.u32());
    difference.index = index;
    PATHOBS_TRY(detail, reader.u8());
    difference.detail = static_cast<DivergenceClass>(detail);
    PATHOBS_TRY(has_expected, reader.boolean());
    PATHOBS_TRY(has_observed, reader.boolean());
    difference.has_expected = has_expected;
    difference.has_observed = has_observed;
    value.differences.push_back(difference);
  }
  PATHOBS_TRY(evaluated_at, reader.i64());
  value.evaluated_at = TimePoint::from_unix_nanos(evaluated_at);

  const ComparisonId recomputed = ComparisonId::from_digest(value.canonical_digest());
  if (recomputed != stored_id) {
    return Error{ErrorCode::IntegrityFailure,
                 "comparison identity does not match its content",
                 "stored " + stored_id.str() + " recomputed " + recomputed.str()};
  }
  value.id = stored_id;
  return value;
}

std::string encode(const Finding& value) {
  CanonicalWriter writer;
  writer.text("pathobs.record.finding");
  writer.digest_id(value.id);
  writer.u8(static_cast<std::uint8_t>(value.cls));
  write_maybe_group(writer, value.group);
  write_maybe_path(writer, value.path);
  writer.id(value.generation);
  writer.id(value.topology);
  writer.count(value.evidence.size());
  for (const auto& evidence_id : value.evidence) {
    writer.digest_id(evidence_id);
  }
  writer.count(value.hop_indices.size());
  for (const auto hop : value.hop_indices) {
    writer.u32(hop);
  }
  writer.u8(static_cast<std::uint8_t>(value.status));
  writer.u64(value.occurrences);
  writer.i64(value.first_seen.unix_nanos());
  writer.i64(value.last_seen.unix_nanos());
  writer.digest_id(value.last_comparison);
  writer.text(value.detail);
  return writer.take();
}

Result<Finding> decode_finding(CanonicalReader& reader, const Limits& limits) {
  PATHOBS_TRY(tag, reader.text(64));
  if (tag != "pathobs.record.finding") {
    return Error{ErrorCode::InvalidFormat, "record is not a finding"};
  }
  Finding value;
  PATHOBS_TRY(stored_id, reader.digest_id<FindingIdTag>());
  PATHOBS_TRY(cls, reader.u8());
  value.cls = static_cast<DivergenceClass>(cls);
  PATHOBS_TRY(group, read_maybe_strong<PathGroupIdTag>(reader));
  value.group = group;
  PATHOBS_TRY(path, read_maybe_strong<PathIdTag>(reader));
  value.path = path;
  PATHOBS_TRY(generation, reader.id<RouteGenerationIdTag>());
  value.generation = generation;
  PATHOBS_TRY(topology, reader.id<TopologyGenerationIdTag>());
  value.topology = topology;
  PATHOBS_TRY(evidence_count, reader.count(limits.max_finding_evidence));
  value.evidence.reserve(evidence_count);
  for (std::uint32_t i = 0; i < evidence_count; ++i) {
    PATHOBS_TRY(evidence_id, reader.digest_id<EvidenceIdTag>());
    value.evidence.push_back(evidence_id);
  }
  PATHOBS_TRY(hop_count, reader.count(limits.max_hops_per_path));
  value.hop_indices.reserve(hop_count);
  for (std::uint32_t i = 0; i < hop_count; ++i) {
    PATHOBS_TRY(hop, reader.u32());
    value.hop_indices.push_back(hop);
  }
  PATHOBS_TRY(status, reader.u8());
  if (status > static_cast<std::uint8_t>(FindingStatus::Resolved)) {
    return Error{ErrorCode::InvalidFormat, "finding status is out of range"};
  }
  value.status = static_cast<FindingStatus>(status);
  PATHOBS_TRY(occurrences, reader.u64());
  value.occurrences = occurrences;
  PATHOBS_TRY(first_seen, reader.i64());
  PATHOBS_TRY(last_seen, reader.i64());
  value.first_seen = TimePoint::from_unix_nanos(first_seen);
  value.last_seen = TimePoint::from_unix_nanos(last_seen);
  PATHOBS_TRY(last_comparison, reader.digest_id<ComparisonIdTag>());
  value.last_comparison = last_comparison;
  PATHOBS_TRY(detail, reader.text(limits.max_string_bytes));
  value.detail = std::move(detail);

  const FindingId recomputed = FindingId::from_digest(value.identity_digest());
  if (recomputed != stored_id) {
    return Error{ErrorCode::IntegrityFailure,
                 "finding identity does not match its content",
                 "stored " + stored_id.str() + " recomputed " + recomputed.str()};
  }
  value.id = stored_id;
  return value;
}

Result<SourceDescriptor> decode_exact<SourceDescriptor>(std::span<const std::byte> payload,
                                                        const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_source_descriptor(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

Result<TopologyGeneration> decode_exact<TopologyGeneration>(std::span<const std::byte> payload,
                                                            const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_topology_generation(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

Result<RouteGeneration> decode_exact<RouteGeneration>(std::span<const std::byte> payload,
                                                      const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_route_generation(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

Result<ObservedPathEvidence> decode_exact<ObservedPathEvidence>(std::span<const std::byte> payload,
                                                                const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_evidence(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

Result<ComparisonResult> decode_exact<ComparisonResult>(std::span<const std::byte> payload,
                                                        const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_comparison(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

Result<Finding> decode_exact<Finding>(std::span<const std::byte> payload, const Limits& limits) {
  CanonicalReader reader(payload);
  PATHOBS_TRY(value, decode_finding(reader, limits));
  const Status ended = reader.expect_end();
  if (!ended.has_value()) {
    return ended.error();
  }
  return value;
}

} // namespace pathobs::codec
