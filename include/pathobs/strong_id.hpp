// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Strongly typed identities.
//
// Path Observatory never passes a bare integer between subsystems when that
// integer denotes a domain object. Every identity is a distinct type derived
// from a tag, so a device identity cannot be substituted for a port identity,
// and a route generation cannot be substituted for a topology generation even
// though both are 64 bit values internally.

#ifndef PATHOBS_STRONG_ID_HPP
#define PATHOBS_STRONG_ID_HPP

#include "pathobs/digest.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <charconv>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace pathobs {

/// A 64 bit identity tagged with a phantom type.
template <class Tag, class Rep = std::uint64_t>
class StrongId {
 public:
  using tag_type = Tag;
  using rep_type = Rep;

  constexpr StrongId() noexcept = default;

  /// Construct without validation. Domain entry points validate by calling
  /// valid(); this constructor exists so that identity values can be built in
  /// constexpr context and in tests.
  static constexpr StrongId from_value(Rep value) noexcept {
    StrongId id;
    id.value_ = value;
    return id;
  }

  /// Parse a decimal identity, rejecting zero and trailing characters.
  static Result<StrongId> parse(std::string_view text) {
    if (text.empty()) {
      return Error{ErrorCode::InvalidIdentity, "identity is empty"};
    }
    Rep value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
      return Error{ErrorCode::InvalidIdentity, "identity is not a decimal number"};
    }
    if (value == 0) {
      return Error{ErrorCode::InvalidIdentity, "identity must not be zero"};
    }
    return from_value(value);
  }

  constexpr Rep value() const noexcept { return value_; }
  constexpr bool valid() const noexcept { return value_ != 0; }

  std::string str() const {
    char buffer[24]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value_);
    return std::string(buffer, static_cast<std::size_t>(result.ptr - buffer));
  }

  friend constexpr bool operator==(const StrongId&, const StrongId&) noexcept = default;
  friend constexpr auto operator<=>(const StrongId&, const StrongId&) noexcept = default;

 private:
  Rep value_{0};
};

// ---------------------------------------------------------------------------
// Identity tags. Each tag names exactly one domain concept.
// ---------------------------------------------------------------------------

struct SourceIdTag;
struct SourceIncarnationIdTag;
struct DeviceIdTag;
struct PortIdTag;
struct LinkIdTag;
struct PathIdTag;
struct HopIdTag;
struct PathGroupIdTag;
struct RouteGenerationIdTag;
struct TopologyGenerationIdTag;
struct EpochIdTag;
struct IncarnationIdTag;
struct RevisionIdTag;
struct SourceSequenceTag;
struct StoreIncarnationIdTag;

struct EvidenceIdTag;
struct ComparisonIdTag;
struct FindingIdTag;

/// Identity of an observation source (a routing authority feed, a telemetry
/// observer, a replay file, a test harness).
using SourceId = StrongId<SourceIdTag>;

/// Identity of one running instance of a source. A restart of a source produces
/// a new incarnation; evidence from a superseded incarnation is fenced.
using SourceIncarnationId = StrongId<SourceIncarnationIdTag>;

using DeviceId = StrongId<DeviceIdTag>;
using PortId = StrongId<PortIdTag>;
using LinkId = StrongId<LinkIdTag>;

using PathId = StrongId<PathIdTag>;

/// Identity of a hop position within a path. Hop identities are assigned by the
/// expected-path model and are stable for the lifetime of a route generation.
using HopId = StrongId<HopIdTag>;

/// Identity of a set of paths that share an origin and a destination. Two or
/// more members of a group is legitimate multipath, not a defect.
using PathGroupId = StrongId<PathGroupIdTag>;

using RouteGenerationId = StrongId<RouteGenerationIdTag>;
using TopologyGenerationId = StrongId<TopologyGenerationIdTag>;

/// Epoch: the coarse fencing boundary. A new epoch invalidates every claim made
/// against an older epoch.
using EpochId = StrongId<EpochIdTag>;

/// Incarnation: identifies one boot of a component within an epoch.
using IncarnationId = StrongId<IncarnationIdTag>;

/// Revision: a monotonic counter within a generation. A replayed revision must
/// never be able to validate current behaviour.
using RevisionId = StrongId<RevisionIdTag>;

/// Source sequence: a monotonic per-incarnation counter supplied by the source.
using SourceSequence = StrongId<SourceSequenceTag>;

/// Identity of one incarnation of a persistence store. Bumped on every open of
/// an existing store so that evidence loaded after a restart can be labelled.
using StoreIncarnationId = StrongId<StoreIncarnationIdTag>;

/// Content derived identities. These are digests of the canonical encoding of
/// the object, which is what makes finding identity stable under reordering.
using EvidenceId = DigestId<EvidenceIdTag>;
using ComparisonId = DigestId<ComparisonIdTag>;
using FindingId = DigestId<FindingIdTag>;

/// A referenced identity that may legitimately be absent, for example the path
/// identity claimed by an observation that could not resolve a path.
template <class Id>
class MaybeId {
 public:
  constexpr MaybeId() noexcept = default;
  constexpr MaybeId(Id id) noexcept : id_(id) {}
  static constexpr MaybeId absent() noexcept { return MaybeId{}; }

  constexpr bool present() const noexcept { return id_.valid(); }
  constexpr Id value() const noexcept { return id_; }
  constexpr Id value_or(Id fallback) const noexcept { return id_.valid() ? id_ : fallback; }

  friend constexpr bool operator==(const MaybeId&, const MaybeId&) noexcept = default;

 private:
  Id id_{};
};

} // namespace pathobs

#endif // PATHOBS_STRONG_ID_HPP
