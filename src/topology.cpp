// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/topology.hpp"

#include <algorithm>

namespace pathobs {

const char* to_string(DeviceRole role) noexcept {
  switch (role) {
    case DeviceRole::Unknown:
      return "unknown";
    case DeviceRole::Switch:
      return "switch";
    case DeviceRole::Router:
      return "router";
    case DeviceRole::Host:
      return "host";
    case DeviceRole::LineCard:
      return "line_card";
    case DeviceRole::Appliance:
      return "appliance";
  }
  return "unknown";
}

Result<DeviceRole> parse_device_role(std::string_view text) {
  if (text == "unknown") return DeviceRole::Unknown;
  if (text == "switch") return DeviceRole::Switch;
  if (text == "router") return DeviceRole::Router;
  if (text == "host") return DeviceRole::Host;
  if (text == "line_card") return DeviceRole::LineCard;
  if (text == "appliance") return DeviceRole::Appliance;
  return Error{ErrorCode::InvalidFormat, "unknown device role", std::string(text)};
}

const char* to_string(LinkKind kind) noexcept {
  switch (kind) {
    case LinkKind::Unknown:
      return "unknown";
    case LinkKind::Physical:
      return "physical";
    case LinkKind::Logical:
      return "logical";
    case LinkKind::Tunnel:
      return "tunnel";
    case LinkKind::Lag:
      return "lag";
  }
  return "unknown";
}

Result<LinkKind> parse_link_kind(std::string_view text) {
  if (text == "unknown") return LinkKind::Unknown;
  if (text == "physical") return LinkKind::Physical;
  if (text == "logical") return LinkKind::Logical;
  if (text == "tunnel") return LinkKind::Tunnel;
  if (text == "lag") return LinkKind::Lag;
  return Error{ErrorCode::InvalidFormat, "unknown link kind", std::string(text)};
}

Digest TopologyGeneration::canonical_digest() const {
  CanonicalWriter writer;
  writer.text("pathobs.topology_generation");
  writer.id(id);
  writer.id(epoch);
  writer.id(incarnation);
  writer.id(revision);
  writer.id(sequence);
  writer.id(source);
  writer.i64(created.unix_nanos());
  writer.count(devices.size());
  for (const auto& device : devices) {
    writer.id(device.id);
    writer.text(device.name);
    writer.u8(static_cast<std::uint8_t>(device.role));
  }
  writer.count(ports.size());
  for (const auto& port : ports) {
    writer.id(port.id);
    writer.id(port.device);
    writer.text(port.name);
    writer.u32(port.index);
  }
  writer.count(links.size());
  for (const auto& link : links) {
    writer.id(link.id);
    writer.id(link.local_device);
    writer.id(link.local_port);
    writer.id(link.remote_device);
    writer.id(link.remote_port);
    writer.u8(static_cast<std::uint8_t>(link.kind));
  }
  return writer.digest();
}

Result<void> TopologyGeneration::finalize(const Limits& limits) {
  const auto device_present = [this](DeviceId candidate) {
    return std::any_of(devices.begin(), devices.end(),
                       [candidate](const DeviceRecord& device) { return device.id == candidate; });
  };

  if (!id.valid() || !epoch.valid() || !incarnation.valid() || !revision.valid() || !source.valid()) {
    return Error{ErrorCode::InvalidIdentity,
                 "topology generation is missing one of generation, epoch, incarnation, revision "
                 "or source"};
  }
  if (devices.size() > limits.max_devices_per_topology) {
    return Error{ErrorCode::CapacityExceeded, "topology device count exceeds the configured bound"};
  }
  if (links.size() > limits.max_links_per_topology) {
    return Error{ErrorCode::CapacityExceeded, "topology link count exceeds the configured bound"};
  }
  if (ports.size() > limits.max_links_per_topology * 2ull) {
    return Error{ErrorCode::CapacityExceeded, "topology port count exceeds the configured bound"};
  }

  std::sort(devices.begin(), devices.end(),
            [](const DeviceRecord& a, const DeviceRecord& b) { return a.id < b.id; });
  std::sort(ports.begin(), ports.end(),
            [](const PortRecord& a, const PortRecord& b) { return a.id < b.id; });
  std::sort(links.begin(), links.end(),
            [](const LinkRecord& a, const LinkRecord& b) { return a.id < b.id; });

  for (std::size_t i = 0; i < devices.size(); ++i) {
    if (!devices[i].id.valid()) {
      return Error{ErrorCode::InvalidIdentity, "topology contains a device without identity"};
    }
    if (i > 0 && devices[i - 1].id == devices[i].id) {
      return Error{ErrorCode::DuplicateIdentity, "topology contains a duplicate device identity"};
    }
  }
  for (std::size_t i = 0; i < ports.size(); ++i) {
    if (!ports[i].id.valid() || !ports[i].device.valid()) {
      return Error{ErrorCode::InvalidIdentity, "topology contains a port without identity"};
    }
    if (i > 0 && ports[i - 1].id == ports[i].id) {
      return Error{ErrorCode::DuplicateIdentity, "topology contains a duplicate port identity"};
    }
    if (!device_present(ports[i].device)) {
      return Error{ErrorCode::UnknownIdentity,
                   "topology port references a device that is not in the generation",
                   ports[i].device.str()};
    }
  }
  for (std::size_t i = 0; i < links.size(); ++i) {
    const LinkRecord& link = links[i];
    if (!link.id.valid() || !link.local_device.valid() || !link.local_port.valid() ||
        !link.remote_device.valid() || !link.remote_port.valid()) {
      return Error{ErrorCode::InvalidIdentity, "topology contains a link without identity"};
    }
    if (i > 0 && links[i - 1].id == links[i].id) {
      return Error{ErrorCode::DuplicateIdentity, "topology contains a duplicate link identity"};
    }
    const auto port_belongs = [this](PortId port, DeviceId device) {
      const auto it = std::lower_bound(ports.begin(), ports.end(), port,
                                       [](const PortRecord& record, PortId candidate) {
                                         return record.id < candidate;
                                       });
      return it != ports.end() && it->id == port && it->device == device;
    };
    if (!port_belongs(link.local_port, link.local_device)) {
      return Error{ErrorCode::UnknownIdentity,
                   "topology link local port does not belong to the local device", link.id.str()};
    }
    if (!port_belongs(link.remote_port, link.remote_device)) {
      return Error{ErrorCode::UnknownIdentity,
                   "topology link remote port does not belong to the remote device", link.id.str()};
    }
  }

  digest = canonical_digest();
  return ok();
}

const DeviceRecord* TopologyGeneration::find_device(DeviceId candidate) const {
  const auto it = std::lower_bound(devices.begin(), devices.end(), candidate,
                                   [](const DeviceRecord& record, DeviceId value) {
                                     return record.id < value;
                                   });
  return it != devices.end() && it->id == candidate ? &*it : nullptr;
}

const PortRecord* TopologyGeneration::find_port(PortId candidate) const {
  const auto it = std::lower_bound(ports.begin(), ports.end(), candidate,
                                   [](const PortRecord& record, PortId value) {
                                     return record.id < value;
                                   });
  return it != ports.end() && it->id == candidate ? &*it : nullptr;
}

const LinkRecord* TopologyGeneration::find_link(LinkId candidate) const {
  const auto it = std::lower_bound(links.begin(), links.end(), candidate,
                                   [](const LinkRecord& record, LinkId value) {
                                     return record.id < value;
                                   });
  return it != links.end() && it->id == candidate ? &*it : nullptr;
}

Result<TopologyIndex> TopologyIndex::build(const TopologyGeneration& generation,
                                           const Limits& limits) {
  if (generation.devices.size() > limits.max_devices_per_topology ||
      generation.links.size() > limits.max_links_per_topology) {
    return Error{ErrorCode::CapacityExceeded, "topology index would exceed the configured bound"};
  }

  TopologyIndex index;
  index.generation_ = &generation;
  for (std::size_t i = 0; i < generation.devices.size(); ++i) {
    index.device_positions_.emplace(generation.devices[i].id, i);
  }
  for (std::size_t i = 0; i < generation.ports.size(); ++i) {
    index.port_positions_.emplace(generation.ports[i].id, i);
  }
  for (std::size_t i = 0; i < generation.links.size(); ++i) {
    const LinkRecord& link = generation.links[i];
    index.link_positions_.emplace(link.id, i);
    index.links_by_endpoint_[HopEndpoint{link.local_device, link.local_port}].push_back(link.id);
    // A link is usable from both of its ends; the reverse direction is recorded
    // so that an observation reported from the far side correlates as well.
    index.links_by_endpoint_[HopEndpoint{link.remote_device, link.remote_port}].push_back(link.id);
  }
  for (auto& entry : index.links_by_endpoint_) {
    auto& candidates = entry.second;
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
  }
  return index;
}

Result<LinkResolution> TopologyIndex::resolve_link(DeviceId device, PortId port) const {
  if (generation_ == nullptr) {
    return Error{ErrorCode::InvalidState, "link correlation requested without a topology index"};
  }
  LinkResolution resolution;
  const auto it = links_by_endpoint_.find(HopEndpoint{device, port});
  if (it == links_by_endpoint_.end()) {
    return resolution;
  }
  resolution.candidates = it->second.size();
  if (it->second.size() == 1) {
    resolution.link = it->second.front();
    resolution.known = true;
    return resolution;
  }
  resolution.ambiguous = true;
  return resolution;
}

bool TopologyIndex::has_device(DeviceId device) const {
  return device_positions_.find(device) != device_positions_.end();
}

bool TopologyIndex::has_port(DeviceId device, PortId port) const {
  const auto it = port_positions_.find(port);
  if (it == port_positions_.end()) {
    return false;
  }
  return generation_ != nullptr && generation_->ports[it->second].device == device;
}

std::size_t TopologyIndex::device_count() const noexcept {
  return generation_ == nullptr ? 0 : generation_->devices.size();
}

std::size_t TopologyIndex::link_count() const noexcept {
  return generation_ == nullptr ? 0 : generation_->links.size();
}

} // namespace pathobs
