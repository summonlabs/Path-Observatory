// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Observation sources and their authority.
//
// Every piece of evidence in Path Observatory names the source that produced
// it, the incarnation of that source, the proof surface the source belongs to,
// and the authority class the source is allowed to speak with. A source never
// gets to claim authority it was not registered with.

#ifndef PATHOBS_SOURCE_HPP
#define PATHOBS_SOURCE_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pathobs {

enum class SourceKind : std::uint8_t {
  Unknown = 0,
  RoutingAuthority = 1,
  TopologyAuthority = 2,
  TelemetryObserver = 3,
  ReplayFile = 4,
  TestHarness = 5,
  Simulator = 6,
};

PATHOBS_API const char* to_string(SourceKind kind) noexcept;
PATHOBS_API Result<SourceKind> parse_source_kind(std::string_view text);

/// Proof surface of a source. This is a labelling obligation, not a marketing
/// field: the runtime reports the surface of every comparison result and refuses
/// to describe synthetic input as real.
enum class ProofSurface : std::uint8_t {
  Unknown = 0,
  /// Evidence produced by real, independently existing infrastructure.
  Real = 1,
  /// Evidence produced by a simulator, replay file, generator or test harness.
  Synthetic = 2,
  /// Evidence whose production path does not exist in this build.
  Unsupported = 3,
};

PATHOBS_API const char* to_string(ProofSurface surface) noexcept;
PATHOBS_API Result<ProofSurface> parse_proof_surface(std::string_view text);

/// How much weight a source's claim carries. Expected path state must come from
/// an Authoritative source; an observation is evidence regardless of class, but
/// an Advisory observation can never be promoted into a compliance statement on
/// its own.
enum class AuthorityClass : std::uint8_t {
  None = 0,
  Informational = 1,
  Advisory = 2,
  Authoritative = 3,
};

PATHOBS_API const char* to_string(AuthorityClass authority) noexcept;
PATHOBS_API Result<AuthorityClass> parse_authority_class(std::string_view text);

struct SourceCapabilities {
  bool expected_state{false};
  bool observed_evidence{false};
  bool topology{false};

  friend bool operator==(const SourceCapabilities&, const SourceCapabilities&) noexcept = default;
};

struct SourceDescriptor {
  SourceId id{};
  std::string name;
  SourceKind kind{SourceKind::Unknown};
  ProofSurface surface{ProofSurface::Unknown};
  AuthorityClass authority{AuthorityClass::None};
  SourceCapabilities capabilities{};
  /// How long after the receive time an observation may still be considered
  /// fresh for this source. Zero means "use the engine default".
  Duration max_observation_age{0};
  /// Largest tolerated difference between the source reported observation time
  /// and the local receive time before the timestamp is treated as skewed.
  Duration clock_skew_tolerance{0};

  Result<void> validate() const;
  Digest canonical_digest() const;
};

/// Registry of the sources the runtime is allowed to trust. Registration is
/// explicit: evidence from an unregistered source is rejected rather than
/// silently admitted.
class PATHOBS_API SourceRegistry {
 public:
  SourceRegistry() = default;

  Status register_source(SourceDescriptor descriptor);
  bool contains(SourceId id) const;
  Result<SourceDescriptor> find(SourceId id) const;
  std::vector<SourceDescriptor> all() const;
  std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::map<SourceId, SourceDescriptor> sources_;
};

/// Per (source, incarnation) sequence ledger.
///
/// The ledger exists to fence replays and to make gaps explicit:
///   * a sequence at or below the highest accepted sequence for the same
///     incarnation is a replay and is rejected;
///   * a sequence one greater than the highest accepted sequence is in order;
///   * a larger jump is accepted but leaves a gap, which downgrades the
///     completeness of everything derived from that source until the missing
///     range is supplied.
class PATHOBS_API SourceSequenceLedger {
 public:
  enum class Verdict : std::uint8_t {
    InOrder = 0,
    Gap = 1,
    Replay = 2,
    NewIncarnation = 3,
  };

  struct Outcome {
    Verdict verdict{Verdict::InOrder};
    std::uint64_t missing{0};
    SourceSequence highest{};
  };

  Outcome observe(SourceId source, SourceIncarnationId incarnation, SourceSequence sequence);

  /// True when the ledger currently holds an unrepaired gap for the source.
  bool has_gap(SourceId source) const;
  std::uint64_t missing_total(SourceId source) const;

  std::size_t tracked_incarnations() const;
  std::map<SourceId, std::uint64_t> gap_counts() const;

 private:
  struct Entry {
    SourceIncarnationId incarnation{};
    SourceSequence highest{};
    std::uint64_t missing{0};
    std::uint64_t accepted{0};
    std::uint64_t replays{0};
  };

  mutable std::mutex mutex_;
  std::map<SourceId, Entry> entries_;
};

} // namespace pathobs

#endif // PATHOBS_SOURCE_HPP
