// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Topology generation model and link correlation.
//
// Path Observatory consumes topology from a topology authority. It never
// computes topology; it only resolves the identities that an observation
// mentions against the generation that the observation claims to belong to.
//
// Link correlation is deliberately conservative: a port that resolves to no
// link, or to more than one link, does not produce a link identity. The hop is
// then reported with unresolved link provenance, which downgrades completeness
// and can never be reported as compliance.

#ifndef PATHOBS_TOPOLOGY_HPP
#define PATHOBS_TOPOLOGY_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/source.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pathobs {

enum class DeviceRole : std::uint8_t {
  Unknown = 0,
  Switch = 1,
  Router = 2,
  Host = 3,
  LineCard = 4,
  Appliance = 5,
};

PATHOBS_API const char* to_string(DeviceRole role) noexcept;
PATHOBS_API Result<DeviceRole> parse_device_role(std::string_view text);

enum class LinkKind : std::uint8_t {
  Unknown = 0,
  Physical = 1,
  Logical = 2,
  Tunnel = 3,
  Lag = 4,
};

PATHOBS_API const char* to_string(LinkKind kind) noexcept;
PATHOBS_API Result<LinkKind> parse_link_kind(std::string_view text);

struct DeviceRecord {
  DeviceId id{};
  std::string name;
  DeviceRole role{DeviceRole::Unknown};

  friend bool operator==(const DeviceRecord&, const DeviceRecord&) noexcept = default;
};

struct PortRecord {
  PortId id{};
  DeviceId device{};
  std::string name;
  std::uint32_t index{0};

  friend bool operator==(const PortRecord&, const PortRecord&) noexcept = default;
};

struct LinkRecord {
  LinkId id{};
  DeviceId local_device{};
  PortId local_port{};
  DeviceId remote_device{};
  PortId remote_port{};
  LinkKind kind{LinkKind::Unknown};

  friend bool operator==(const LinkRecord&, const LinkRecord&) noexcept = default;
};

/// A device/port pair. This is the unit that link correlation resolves.
struct HopEndpoint {
  DeviceId device{};
  PortId port{};

  friend constexpr bool operator==(const HopEndpoint&, const HopEndpoint&) noexcept = default;
  friend constexpr auto operator<=>(const HopEndpoint&, const HopEndpoint&) noexcept = default;
};

struct TopologyGeneration {
  TopologyGenerationId id{};
  EpochId epoch{};
  IncarnationId incarnation{};
  RevisionId revision{};
  SourceSequence sequence{};
  SourceId source{};
  TimePoint created{};
  TimePoint received{};
  std::vector<DeviceRecord> devices;
  std::vector<PortRecord> ports;
  std::vector<LinkRecord> links;
  Digest digest{};

  /// Validate, sort by identity and compute the content digest. Any failure
  /// leaves the generation unusable; the runtime never keeps a generation that
  /// failed validation.
  Result<void> finalize(const Limits& limits);

  const DeviceRecord* find_device(DeviceId id) const;
  const PortRecord* find_port(PortId id) const;
  const LinkRecord* find_link(LinkId id) const;

  Digest canonical_digest() const;
};

struct LinkResolution {
  LinkId link{};
  bool known{false};
  bool ambiguous{false};
  std::size_t candidates{0};

  friend bool operator==(const LinkResolution&, const LinkResolution&) noexcept = default;
};

/// Indexed view over one topology generation. Built once, queried many times.
class PATHOBS_API TopologyIndex {
 public:
  TopologyIndex() = default;

  static Result<TopologyIndex> build(const TopologyGeneration& generation, const Limits& limits);

  const TopologyGeneration& generation() const noexcept { return *generation_; }
  bool valid() const noexcept { return generation_ != nullptr; }

  /// Resolve the link attached to a device/port pair. A port that is unknown,
  /// or that appears in more than one link, yields known == false.
  Result<LinkResolution> resolve_link(DeviceId device, PortId port) const;

  bool has_device(DeviceId device) const;
  bool has_port(DeviceId device, PortId port) const;
  std::size_t device_count() const noexcept;
  std::size_t link_count() const noexcept;

 private:
  const TopologyGeneration* generation_{nullptr};
  std::map<DeviceId, std::size_t> device_positions_;
  std::map<PortId, std::size_t> port_positions_;
  std::map<LinkId, std::size_t> link_positions_;
  std::map<HopEndpoint, std::vector<LinkId>> links_by_endpoint_;
};

} // namespace pathobs

#endif // PATHOBS_TOPOLOGY_HPP
