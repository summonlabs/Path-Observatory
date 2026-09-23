// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Interchange documents.
//
// Routing authority state and observation evidence enter Path Observatory as
// JSON documents. Every reader is strict about what it requires and explicit
// about what is optional; every writer is deterministic, so exporting the same
// object twice produces byte identical output.

#ifndef PATHOBS_DOCUMENTS_HPP
#define PATHOBS_DOCUMENTS_HPP

#include "pathobs/compare.hpp"
#include "pathobs/error.hpp"
#include "pathobs/expected.hpp"
#include "pathobs/export.hpp"
#include "pathobs/finding.hpp"
#include "pathobs/history.hpp"
#include "pathobs/json.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/metrics.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/persistence.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/source.hpp"
#include "pathobs/topology.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace pathobs::documents {

/// Document kinds. The kind field is mandatory: a document that does not say
/// what it is cannot be interpreted by guessing.
inline constexpr std::string_view kSourceKindTag = "pathobs.source";
inline constexpr std::string_view kTopologyKindTag = "pathobs.topology_generation";
inline constexpr std::string_view kRoutesKindTag = "pathobs.route_generation";
inline constexpr std::string_view kEvidenceKindTag = "pathobs.evidence";

PATHOBS_API Result<SourceDescriptor> parse_source_descriptor(std::string_view text,
                                                             const Limits& limits);
PATHOBS_API Result<TopologyGeneration> parse_topology_generation(std::string_view text,
                                                                 const Limits& limits);
PATHOBS_API Result<RouteGeneration> parse_route_generation(std::string_view text,
                                                           const Limits& limits);
PATHOBS_API Result<ObservedPathEvidence> parse_evidence(std::string_view text,
                                                        const Limits& limits);

/// Parse a JSON Lines stream of evidence records. Blank lines and lines whose
/// first non-space character is a hash are ignored; a malformed line is an
/// error rather than a silently skipped record.
PATHOBS_API Result<std::vector<ObservedPathEvidence>> parse_evidence_lines(std::string_view text,
                                                                           const Limits& limits);

PATHOBS_API std::string to_json(const SourceDescriptor& value, bool pretty);
PATHOBS_API std::string to_json(const TopologyGeneration& value, bool pretty);
PATHOBS_API std::string to_json(const RouteGeneration& value, bool pretty);
PATHOBS_API std::string to_json(const ObservedPathEvidence& value, bool pretty);
PATHOBS_API std::string to_json(const ComparisonResult& value, bool pretty);
PATHOBS_API std::string to_json(const Finding& value, bool pretty);
PATHOBS_API std::string to_json(const HistoryRecord& value, bool pretty);
PATHOBS_API std::string to_json(const HistoryQueryResult& value, bool pretty);
PATHOBS_API std::string to_json(const RecoveryReport& value, bool pretty);
PATHOBS_API std::string to_json(const MetricsSnapshot& value, bool pretty);
PATHOBS_API std::string to_json(const RuntimeStats& value, bool pretty);

} // namespace pathobs::documents

#endif // PATHOBS_DOCUMENTS_HPP
