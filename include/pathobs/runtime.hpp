// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The observatory runtime.
//
// The runtime owns the observation pipeline and nothing else. It consumes
// expected path state from a routing authority, consumes observed path evidence
// from observation sources, compares the two and reports. It never computes a
// route, never selects an alternate, never mutates forwarding and never treats
// a legal path as proof of delivery.
//
// Restart policy
// --------------
// Expected state is never restored from disk. A restarted runtime begins with
// no route generation and refuses to compare until an authority publishes one,
// because a restored generation is exactly the stale-plan-validating-current-
// behaviour failure this component exists to prevent. Evidence IS restored, but
// it keeps its original receive time, so it is re-evaluated as stale rather
// than silently becoming fresh.

#ifndef PATHOBS_RUNTIME_HPP
#define PATHOBS_RUNTIME_HPP

#include "pathobs/compare.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/finding.hpp"
#include "pathobs/history.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/metrics.hpp"
#include "pathobs/observed.hpp"
#include "pathobs/persistence.hpp"
#include "pathobs/source.hpp"
#include "pathobs/time.hpp"
#include "pathobs/worker.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pathobs {

enum class RuntimeState : std::uint8_t {
  Created = 0,
  Running = 1,
  Stopping = 2,
  Stopped = 3,
  Failed = 4,
};

PATHOBS_API const char* to_string(RuntimeState state) noexcept;

struct RuntimeConfig {
  Limits limits{};
  ComparisonPolicy comparison{};
  /// Empty means "no persistence".
  std::filesystem::path store_path{};
  bool persist{false};
  bool load_evidence_on_start{true};
  std::uint32_t worker_threads{0};
  bool mark_store_clean_on_stop{true};
};

/// The bucket an evidence record belongs to: one route generation and, when it
/// can be resolved, one path group.
struct SubjectKey {
  RouteGenerationId generation{};
  MaybeId<PathGroupId> group{};

  friend bool operator==(const SubjectKey&, const SubjectKey&) noexcept = default;
  friend bool operator<(const SubjectKey& a, const SubjectKey& b) noexcept {
    if (a.generation != b.generation) {
      return a.generation < b.generation;
    }
    if (a.group.present() != b.group.present()) {
      return !a.group.present();
    }
    return a.group.value() < b.group.value();
  }
};

struct IngestResult {
  bool accepted{false};
  DivergenceClass reason{DivergenceClass::None};
  EvidenceId id{};
  bool sequence_gap{false};
  bool replay{false};
  bool persisted{false};
  std::string detail;
};

struct RuntimeStats {
  RuntimeState state{RuntimeState::Created};
  MetricsSnapshot metrics{};
  std::size_t evidence_buckets{0};
  std::size_t evidence_records{0};
  std::size_t findings{0};
  std::size_t findings_open{0};
  std::size_t history{0};
  std::size_t queue_pending{0};
  EpochId epoch{};
  IncarnationId incarnation{};
  RouteGenerationId route_generation{};
  TopologyGenerationId topology_generation{};
  RevisionId route_revision{};
  RecoveryReport recovery{};
  bool has_route_generation{false};
  bool has_topology_generation{false};
};

class PATHOBS_API ObservatoryRuntime {
 public:
  static Result<std::unique_ptr<ObservatoryRuntime>> create(RuntimeConfig config,
                                                            std::unique_ptr<Clock> clock);

  ~ObservatoryRuntime();

  ObservatoryRuntime(const ObservatoryRuntime&) = delete;
  ObservatoryRuntime& operator=(const ObservatoryRuntime&) = delete;

  Status start();
  Status stop();
  RuntimeState state() const;

  // --- authority side ------------------------------------------------------
  Status register_source(SourceDescriptor descriptor);
  Result<SourceDescriptor> source(SourceId id) const;
  std::vector<SourceDescriptor> sources() const;

  Status publish_topology(TopologyGeneration generation);
  Status publish_routes(RouteGeneration generation);
  bool has_topology() const;
  bool has_routes() const;
  AuthorityState authority_state() const;
  Result<TopologyGeneration> topology() const;
  Result<RouteGeneration> routes() const;

  // --- observation side ----------------------------------------------------
  Result<IngestResult> ingest_evidence(ObservedPathEvidence evidence);
  Result<std::size_t> ingest_batch(const std::vector<ObservedPathEvidence>& batch);
  std::size_t pending_batches() const;
  Status submit_batch(std::vector<ObservedPathEvidence> batch);
  Status drain();

  // --- comparison ----------------------------------------------------------
  Result<ComparisonResult> compare_group(PathGroupId group);
  Result<ComparisonResult> compare_path(PathId path);
  Result<std::vector<ComparisonResult>> compare_all();

  // --- queries -------------------------------------------------------------
  Result<HistoryQueryResult> history(const HistoryQuery& request) const;
  std::vector<Finding> findings() const;
  Result<Finding> finding(FindingId id) const;
  Status acknowledge_finding(FindingId id);
  Result<std::size_t> prune_history();

  // --- persistence ---------------------------------------------------------
  Status flush();
  bool persistent() const;
  RecoveryReport recovery() const;

  RuntimeStats stats() const;
  const Limits& limits() const noexcept { return config_.limits; }
  const ComparisonPolicy& comparison_policy() const noexcept { return config_.comparison; }
  Metrics& metrics() noexcept { return metrics_; }
  const Metrics& metrics() const noexcept { return metrics_; }
  TimePoint now() const;

 private:
  explicit ObservatoryRuntime(RuntimeConfig config, std::unique_ptr<Clock> clock);

  Status open_store();
  Status persist_evidence(const ObservedPathEvidence& evidence);
  Status persist_comparison(const ComparisonResult& result);
  Status restore_from_store();
  Result<SubjectKey> resolve_subject(const ObservedPathEvidence& evidence) const;
  Result<ComparisonResult> compare_bucket(const SubjectKey& key, bool record);
  ComparisonContext make_context() const;

  RuntimeConfig config_;
  std::unique_ptr<Clock> clock_;
  mutable std::mutex mutex_;

  SourceRegistry sources_{};
  SourceSequenceLedger ledger_{};
  std::optional<TopologyGeneration> topology_{};
  std::optional<TopologyIndex> topology_index_{};
  std::optional<RouteGeneration> routes_{};
  std::map<SubjectKey, std::vector<ObservedPathEvidence>> evidence_{};
  std::map<SourceId, bool> source_has_gap_{};

  HistoryStore history_;
  FindingRegistry findings_;
  Metrics metrics_{};
  std::optional<PersistentStore> store_{};
  std::unique_ptr<WorkerPool> pool_{};
  RuntimeState state_{RuntimeState::Created};
  EpochId epoch_{};
  IncarnationId incarnation_{};
  RevisionId route_revision_{};
  RevisionId topology_revision_{};
};

} // namespace pathobs

#endif // PATHOBS_RUNTIME_HPP
