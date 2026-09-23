// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Canonical record encoding for persistence and for the wire.
//
// One encoder and one decoder serve both, so a record cannot mean one thing on
// disk and another on the wire. Decoding recomputes every content derived
// identity and rejects a record whose identity does not match its bytes: a
// rewritten payload cannot pass as the record it claims to be.

#ifndef PATHOBS_CODEC_HPP
#define PATHOBS_CODEC_HPP

#include "pathobs/canonical.hpp"
#include "pathobs/compare.hpp"
#include "pathobs/error.hpp"
#include "pathobs/expected.hpp"
#include "pathobs/export.hpp"
#include "pathobs/finding.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/source.hpp"
#include "pathobs/topology.hpp"

#include <span>
#include <string>

namespace pathobs::codec {

PATHOBS_API std::string encode(const SourceDescriptor& value);
PATHOBS_API Result<SourceDescriptor> decode_source_descriptor(CanonicalReader& reader,
                                                              const Limits& limits);

PATHOBS_API std::string encode(const TopologyGeneration& value);
PATHOBS_API Result<TopologyGeneration> decode_topology_generation(CanonicalReader& reader,
                                                                 const Limits& limits);

PATHOBS_API std::string encode(const RouteGeneration& value);
PATHOBS_API Result<RouteGeneration> decode_route_generation(CanonicalReader& reader,
                                                            const Limits& limits);

PATHOBS_API std::string encode(const ObservedPathEvidence& value);
PATHOBS_API Result<ObservedPathEvidence> decode_evidence(CanonicalReader& reader,
                                                         const Limits& limits);

PATHOBS_API std::string encode(const ComparisonResult& value);
PATHOBS_API Result<ComparisonResult> decode_comparison(CanonicalReader& reader,
                                                       const Limits& limits);

PATHOBS_API std::string encode(const Finding& value);
PATHOBS_API Result<Finding> decode_finding(CanonicalReader& reader, const Limits& limits);

/// Decode a complete payload, refusing trailing bytes. The specialisations are
/// declared here so that a caller in another translation unit links against the
/// same decoder rather than instantiating its own.
template <class T>
Result<T> decode_exact(std::span<const std::byte> payload, const Limits& limits);

template <>
PATHOBS_API Result<SourceDescriptor> decode_exact<SourceDescriptor>(std::span<const std::byte> payload,
                                                                    const Limits& limits);
template <>
PATHOBS_API Result<TopologyGeneration> decode_exact<TopologyGeneration>(
    std::span<const std::byte> payload, const Limits& limits);
template <>
PATHOBS_API Result<RouteGeneration> decode_exact<RouteGeneration>(std::span<const std::byte> payload,
                                                                  const Limits& limits);
template <>
PATHOBS_API Result<ObservedPathEvidence> decode_exact<ObservedPathEvidence>(
    std::span<const std::byte> payload, const Limits& limits);
template <>
PATHOBS_API Result<ComparisonResult> decode_exact<ComparisonResult>(std::span<const std::byte> payload,
                                                                    const Limits& limits);
template <>
PATHOBS_API Result<Finding> decode_exact<Finding>(std::span<const std::byte> payload,
                                                  const Limits& limits);

} // namespace pathobs::codec

#endif // PATHOBS_CODEC_HPP
