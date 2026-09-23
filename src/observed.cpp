// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/observed.hpp"

#include <algorithm>

namespace pathobs {

const char* to_string(HopLinkProvenance provenance) noexcept {
  switch (provenance) {
    case HopLinkProvenance::Absent:
      return "absent";
    case HopLinkProvenance::Observed:
      return "observed";
    case HopLinkProvenance::Correlated:
      return "correlated";
  }
  return "absent";
}

Result<HopLinkProvenance> parse_hop_link_provenance(std::string_view text) {
  if (text == "absent") return HopLinkProvenance::Absent;
  if (text == "observed") return HopLinkProvenance::Observed;
  if (text == "correlated") return HopLinkProvenance::Correlated;
  return Error{ErrorCode::InvalidFormat, "unknown hop link provenance", std::string(text)};
}

const char* to_string(EvidenceCompleteness completeness) noexcept {
  switch (completeness) {
    case EvidenceCompleteness::Unknown:
      return "unknown";
    case EvidenceCompleteness::Partial:
      return "partial";
    case EvidenceCompleteness::Complete:
      return "complete";
  }
  return "unknown";
}

Result<EvidenceCompleteness> parse_evidence_completeness(std::string_view text) {
  if (text == "unknown") return EvidenceCompleteness::Unknown;
  if (text == "partial") return EvidenceCompleteness::Partial;
  if (text == "complete") return EvidenceCompleteness::Complete;
  return Error{ErrorCode::InvalidFormat, "unknown evidence completeness", std::string(text)};
}

bool hop_indices_are_contiguous(const std::vector<ObservedHop>& hops) {
  for (std::size_t i = 0; i < hops.size(); ++i) {
    if (hops[i].index != i) {
      return false;
    }
  }
  return true;
}

Result<void> ObservedPathEvidence::validate(const Limits& limits) const {
  if (!source.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence has no source identity"};
  }
  if (!incarnation.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence has no source incarnation"};
  }
  if (!sequence.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence has no source sequence"};
  }
  if (!epoch.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence does not state an epoch"};
  }
  if (!claimed_incarnation.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence does not state a component incarnation"};
  }
  if (!revision.valid()) {
    return Error{ErrorCode::InvalidIdentity, "evidence does not state a revision"};
  }
  if (surface == ProofSurface::Unknown) {
    return Error{ErrorCode::InvalidArgument, "evidence does not state a proof surface"};
  }
  if (authority == AuthorityClass::None) {
    return Error{ErrorCode::InvalidArgument, "evidence does not state an authority class"};
  }
  if (hops.size() > limits.max_hops_per_path) {
    return Error{ErrorCode::CapacityExceeded, "evidence hop count exceeds the configured bound"};
  }
  if (note.size() > 4096) {
    return Error{ErrorCode::OutOfRange, "evidence note exceeds the configured bound"};
  }
  if (id.valid()) {
    // A record that carries an identity must carry the identity its content
    // produces. Without this check a rewritten record would be silently treated
    // as a duplicate of the record whose identity it copied.
    const EvidenceId recomputed = compute_evidence_id(*this);
    if (recomputed != id) {
      return Error{ErrorCode::IntegrityFailure,
                   "evidence identity does not match its content",
                   "stated " + id.str() + " recomputed " + recomputed.str()};
    }
  }
  if (!observed_at.is_set() && !received_at.is_set()) {
    return Error{ErrorCode::InvalidArgument, "evidence carries neither an observation nor a "
                                             "receive timestamp"};
  }
  for (std::size_t i = 0; i < hops.size(); ++i) {
    const ObservedHop& hop = hops[i];
    if (!hop.device.valid()) {
      return Error{ErrorCode::InvalidIdentity, "observed hop has no device identity"};
    }
    if (hop.link.valid() && hop.link_provenance == HopLinkProvenance::Absent) {
      return Error{ErrorCode::InvalidArgument,
                   "observed hop states a link identity without saying how it was obtained"};
    }
    if (!hop.link.valid() && hop.link_provenance != HopLinkProvenance::Absent) {
      return Error{ErrorCode::InvalidArgument,
                   "observed hop claims link provenance without a link identity"};
    }
    if (i > 0 && hop.index <= hops[i - 1].index) {
      return Error{ErrorCode::InvalidArgument,
                   "observed hop indices are not strictly ascending"};
    }
  }
  // Destinations must be distinct when they are stated: a path whose origin and
  // destination are the same device is a loop, and loop containment is not this
  // component's authority. Report it as a shape problem instead of guessing.
  if (origin.present() && destination.present() && origin.value() == destination.value() &&
      hops.size() > 1) {
    return Error{ErrorCode::InvalidArgument,
                 "evidence states identical origin and destination for a multi hop observation"};
  }
  return ok();
}

void ObservedPathEvidence::write_canonical(CanonicalWriter& writer) const {
  writer.text("pathobs.evidence");
  writer.id(source);
  writer.id(incarnation);
  writer.id(sequence);
  writer.u8(static_cast<std::uint8_t>(surface));
  writer.u8(static_cast<std::uint8_t>(authority));
  writer.boolean(path.present());
  writer.id(path.value());
  writer.boolean(group.present());
  writer.id(group.value());
  writer.boolean(origin.present());
  writer.id(origin.value());
  writer.boolean(destination.present());
  writer.id(destination.value());
  writer.id(epoch);
  writer.id(claimed_incarnation);
  writer.boolean(route_generation.present());
  writer.id(route_generation.value());
  writer.boolean(topology_generation.present());
  writer.id(topology_generation.value());
  writer.id(revision);
  writer.i64(observed_at.unix_nanos());
  writer.boolean(source_truncated);
  writer.u8(static_cast<std::uint8_t>(declared_completeness));
  writer.digest(payload_digest);
  writer.text(note);
  writer.count(hops.size());
  for (const auto& hop : hops) {
    writer.u32(hop.index);
    writer.id(hop.device);
    writer.id(hop.ingress);
    writer.id(hop.egress);
    writer.id(hop.link);
    writer.u8(static_cast<std::uint8_t>(hop.link_provenance));
    writer.i64(hop.at.unix_nanos());
  }
}

Digest ObservedPathEvidence::canonical_digest() const {
  CanonicalWriter writer;
  write_canonical(writer);
  return writer.digest();
}

void ObservedPathEvidence::seal() {
  CanonicalWriter writer;
  write_canonical(writer);
  id = EvidenceId::from_digest(writer.digest());
}

EvidenceCompleteness ObservedPathEvidence::claimed_completeness() const noexcept {
  if (source_truncated) {
    return EvidenceCompleteness::Partial;
  }
  if (declared_completeness != EvidenceCompleteness::Unknown) {
    return declared_completeness;
  }
  return EvidenceCompleteness::Unknown;
}

EvidenceId compute_evidence_id(const ObservedPathEvidence& evidence) {
  CanonicalWriter writer;
  evidence.write_canonical(writer);
  return EvidenceId::from_digest(writer.digest());
}

} // namespace pathobs
