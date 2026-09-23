// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/source.hpp"

namespace pathobs {

const char* to_string(SourceKind kind) noexcept {
  switch (kind) {
    case SourceKind::Unknown:
      return "unknown";
    case SourceKind::RoutingAuthority:
      return "routing_authority";
    case SourceKind::TopologyAuthority:
      return "topology_authority";
    case SourceKind::TelemetryObserver:
      return "telemetry_observer";
    case SourceKind::ReplayFile:
      return "replay_file";
    case SourceKind::TestHarness:
      return "test_harness";
    case SourceKind::Simulator:
      return "simulator";
  }
  return "unknown";
}

Result<SourceKind> parse_source_kind(std::string_view text) {
  if (text == "unknown") return SourceKind::Unknown;
  if (text == "routing_authority") return SourceKind::RoutingAuthority;
  if (text == "topology_authority") return SourceKind::TopologyAuthority;
  if (text == "telemetry_observer") return SourceKind::TelemetryObserver;
  if (text == "replay_file") return SourceKind::ReplayFile;
  if (text == "test_harness") return SourceKind::TestHarness;
  if (text == "simulator") return SourceKind::Simulator;
  return Error{ErrorCode::InvalidFormat, "unknown source kind", std::string(text)};
}

const char* to_string(ProofSurface surface) noexcept {
  switch (surface) {
    case ProofSurface::Unknown:
      return "unknown";
    case ProofSurface::Real:
      return "real";
    case ProofSurface::Synthetic:
      return "synthetic";
    case ProofSurface::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

Result<ProofSurface> parse_proof_surface(std::string_view text) {
  if (text == "unknown") return ProofSurface::Unknown;
  if (text == "real") return ProofSurface::Real;
  if (text == "synthetic") return ProofSurface::Synthetic;
  if (text == "unsupported") return ProofSurface::Unsupported;
  return Error{ErrorCode::InvalidFormat, "unknown proof surface", std::string(text)};
}

const char* to_string(AuthorityClass authority) noexcept {
  switch (authority) {
    case AuthorityClass::None:
      return "none";
    case AuthorityClass::Informational:
      return "informational";
    case AuthorityClass::Advisory:
      return "advisory";
    case AuthorityClass::Authoritative:
      return "authoritative";
  }
  return "none";
}

Result<AuthorityClass> parse_authority_class(std::string_view text) {
  if (text == "none") return AuthorityClass::None;
  if (text == "informational") return AuthorityClass::Informational;
  if (text == "advisory") return AuthorityClass::Advisory;
  if (text == "authoritative") return AuthorityClass::Authoritative;
  return Error{ErrorCode::InvalidFormat, "unknown authority class", std::string(text)};
}

Result<void> SourceDescriptor::validate() const {
  if (!id.valid()) {
    return Error{ErrorCode::InvalidIdentity, "source descriptor has no identity"};
  }
  if (name.empty()) {
    return Error{ErrorCode::InvalidArgument, "source descriptor has no name"};
  }
  if (name.size() > 256) {
    return Error{ErrorCode::OutOfRange, "source name is too long"};
  }
  if (kind == SourceKind::Unknown) {
    return Error{ErrorCode::InvalidArgument, "source kind must be stated"};
  }
  if (surface == ProofSurface::Unknown) {
    return Error{ErrorCode::InvalidArgument,
                 "source proof surface must be stated as real, synthetic or unsupported"};
  }
  if (authority == AuthorityClass::None) {
    return Error{ErrorCode::InvalidArgument, "source authority class must be stated"};
  }
  const bool routing_capable = capabilities.expected_state || capabilities.topology;
  if (routing_capable && authority != AuthorityClass::Authoritative) {
    return Error{ErrorCode::InvalidArgument,
                 "a source that publishes expected state must be authoritative"};
  }
  if (!capabilities.expected_state && !capabilities.observed_evidence && !capabilities.topology) {
    return Error{ErrorCode::InvalidArgument, "source declares no capability"};
  }
  if (max_observation_age.count() < 0) {
    return Error{ErrorCode::InvalidArgument, "source max observation age must not be negative"};
  }
  if (clock_skew_tolerance.count() < 0) {
    return Error{ErrorCode::InvalidArgument, "source clock skew tolerance must not be negative"};
  }
  return ok();
}

Digest SourceDescriptor::canonical_digest() const {
  CanonicalWriter writer;
  writer.text("pathobs.source");
  writer.id(id);
  writer.text(name);
  writer.u8(static_cast<std::uint8_t>(kind));
  writer.u8(static_cast<std::uint8_t>(surface));
  writer.u8(static_cast<std::uint8_t>(authority));
  writer.boolean(capabilities.expected_state);
  writer.boolean(capabilities.observed_evidence);
  writer.boolean(capabilities.topology);
  writer.i64(max_observation_age.count());
  writer.i64(clock_skew_tolerance.count());
  return writer.digest();
}

Status SourceRegistry::register_source(SourceDescriptor descriptor) {
  const Status valid = descriptor.validate();
  if (!valid.has_value()) {
    return valid.error();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = sources_.find(descriptor.id);
  if (existing != sources_.end()) {
    if (existing->second.canonical_digest() == descriptor.canonical_digest()) {
      return ok();
    }
    return Error{ErrorCode::DuplicateIdentity,
                 "a different source is already registered with this identity"};
  }
  sources_.emplace(descriptor.id, std::move(descriptor));
  return ok();
}

bool SourceRegistry::contains(SourceId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sources_.find(id) != sources_.end();
}

Result<SourceDescriptor> SourceRegistry::find(SourceId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sources_.find(id);
  if (it == sources_.end()) {
    return Error{ErrorCode::UnknownIdentity, "source is not registered", id.str()};
  }
  return it->second;
}

std::vector<SourceDescriptor> SourceRegistry::all() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SourceDescriptor> out;
  out.reserve(sources_.size());
  for (const auto& entry : sources_) {
    out.push_back(entry.second);
  }
  return out;
}

std::size_t SourceRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sources_.size();
}

SourceSequenceLedger::Outcome SourceSequenceLedger::observe(SourceId source,
                                                           SourceIncarnationId incarnation,
                                                           SourceSequence sequence) {
  std::lock_guard<std::mutex> lock(mutex_);
  Entry& entry = entries_[source];
  Outcome outcome;

  if (!entry.incarnation.valid() || entry.incarnation != incarnation) {
    if (entry.incarnation.valid()) {
      // A new incarnation restarts the sequence space. The old high water mark
      // is discarded, which is precisely why the incarnation is part of the
      // fencing tuple: a replay from the superseded incarnation is still
      // rejected by the incarnation check in the comparison engine.
      outcome.verdict = Verdict::NewIncarnation;
      entry.incarnation = incarnation;
      entry.highest = sequence;
      entry.missing = 0;
      ++entry.accepted;
      outcome.highest = sequence;
      return outcome;
    }
    entry.incarnation = incarnation;
    entry.highest = sequence;
    ++entry.accepted;
    outcome.highest = sequence;
    return outcome;
  }

  if (sequence <= entry.highest) {
    ++entry.replays;
    outcome.verdict = Verdict::Replay;
    outcome.highest = entry.highest;
    return outcome;
  }

  const std::uint64_t previous = entry.highest.value();
  const std::uint64_t current = sequence.value();
  const std::uint64_t gap = current - previous - 1;
  if (gap > 0) {
    entry.missing += gap;
    outcome.verdict = Verdict::Gap;
    outcome.missing = gap;
  }
  entry.highest = sequence;
  ++entry.accepted;
  outcome.highest = sequence;
  return outcome;
}

bool SourceSequenceLedger::has_gap(SourceId source) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(source);
  return it != entries_.end() && it->second.missing > 0;
}

std::uint64_t SourceSequenceLedger::missing_total(SourceId source) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(source);
  return it == entries_.end() ? 0 : it->second.missing;
}

std::size_t SourceSequenceLedger::tracked_incarnations() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

std::map<SourceId, std::uint64_t> SourceSequenceLedger::gap_counts() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<SourceId, std::uint64_t> out;
  for (const auto& entry : entries_) {
    out.emplace(entry.first, entry.second.missing);
  }
  return out;
}

} // namespace pathobs
