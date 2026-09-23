// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/runtime.hpp"

#include "pathobs/codec.hpp"

#include <algorithm>
#include <thread>

namespace pathobs {
namespace {

constexpr std::size_t kMaxPendingBatches = 256;

} // namespace

const char* to_string(RuntimeState state) noexcept {
  switch (state) {
    case RuntimeState::Created:
      return "created";
    case RuntimeState::Running:
      return "running";
    case RuntimeState::Stopping:
      return "stopping";
    case RuntimeState::Stopped:
      return "stopped";
    case RuntimeState::Failed:
      return "failed";
  }
  return "created";
}

ObservatoryRuntime::ObservatoryRuntime(RuntimeConfig config, std::unique_ptr<Clock> clock)
    : config_(std::move(config)),
      clock_(std::move(clock)),
      history_(config_.limits),
      findings_(config_.limits) {}

ObservatoryRuntime::~ObservatoryRuntime() {
  if (pool_) {
    pool_->shutdown();
  }
}

Result<std::unique_ptr<ObservatoryRuntime>> ObservatoryRuntime::create(RuntimeConfig config,
                                                                       std::unique_ptr<Clock> clock) {
  const Status limits_valid = config.limits.validate();
  if (!limits_valid.has_value()) {
    return limits_valid.error();
  }
  const Status policy_valid = config.comparison.validate();
  if (!policy_valid.has_value()) {
    return policy_valid.error();
  }
  if (clock == nullptr) {
    return Error{ErrorCode::InvalidArgument, "the runtime requires a clock"};
  }
  if (config.persist && config.store_path.empty()) {
    return Error{ErrorCode::InvalidArgument, "persistence was requested without a store path"};
  }
  auto runtime =
      std::unique_ptr<ObservatoryRuntime>(new ObservatoryRuntime(std::move(config), std::move(clock)));
  return runtime;
}

Status ObservatoryRuntime::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == RuntimeState::Running) {
    return Error{ErrorCode::InvalidState, "runtime is already running"};
  }
  if (state_ == RuntimeState::Stopping) {
    return Error{ErrorCode::InvalidState, "runtime is stopping"};
  }

  const std::uint32_t threads =
      config_.worker_threads == 0 ? config_.limits.worker_threads : config_.worker_threads;
  if (threads == 0 || threads > config_.limits.max_worker_threads) {
    return Error{ErrorCode::OutOfRange, "worker thread count is outside the configured bounds"};
  }
  pool_ = std::make_unique<WorkerPool>(threads, config_.limits.max_queue_depth);
  const Status started = pool_->start();
  if (!started.has_value()) {
    state_ = RuntimeState::Failed;
    return started.error();
  }

  if (config_.persist) {
    const Status opened = open_store();
    if (!opened.has_value()) {
      state_ = RuntimeState::Failed;
      return opened.error();
    }
    if (config_.load_evidence_on_start) {
      const Status restored = restore_from_store();
      if (!restored.has_value()) {
        state_ = RuntimeState::Failed;
        return restored.error();
      }
    }
  }

  // Every epoch, incarnation and revision starts empty: nothing published by a
  // previous process is assumed to still be current.
  epoch_ = EpochId{};
  incarnation_ = IncarnationId{};
  route_revision_ = RevisionId{};
  topology_revision_ = RevisionId{};
  topology_.reset();
  topology_index_.reset();
  routes_.reset();

  state_ = RuntimeState::Running;
  return ok();
}

Status ObservatoryRuntime::stop() {
  RuntimeState previous = RuntimeState::Created;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == RuntimeState::Stopped) {
      return ok();
    }
    previous = state_;
    state_ = RuntimeState::Stopping;
  }
  if (pool_) {
    pool_->shutdown();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (store_.has_value()) {
    if (config_.mark_store_clean_on_stop) {
      const Status marked = store_->mark_clean(now());
      if (!marked.has_value()) {
        state_ = RuntimeState::Failed;
        return marked.error();
      }
    }
  }
  static_cast<void>(previous);
  state_ = RuntimeState::Stopped;
  return ok();
}

RuntimeState ObservatoryRuntime::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

TimePoint ObservatoryRuntime::now() const { return clock_ == nullptr ? TimePoint{} : clock_->now(); }

Status ObservatoryRuntime::open_store() {
  StoreOptions options;
  options.limits = config_.limits;
  Result<PersistentStore> opened = PersistentStore::open(config_.store_path, options, now());
  if (!opened.has_value()) {
    return opened.error();
  }
  store_.emplace(std::move(opened).value());
  metrics_.add_store_records_recovered(store_->recovery().valid_records);
  if (store_->recovery().corrupt_tail_truncated) {
    metrics_.add_store_corrupt_tail_truncation();
  }
  return ok();
}

Status ObservatoryRuntime::restore_from_store() {
  if (!store_.has_value()) {
    return Error{ErrorCode::InvalidState, "no store is open"};
  }
  PATHOBS_TRY(records, store_->read_all());
  // Expected state is deliberately NOT restored. Only observation records, the
  // comparison history and findings come back.
  //
  // A record that cannot be decoded is a hard failure, and the report says
  // exactly which record it was: a store that silently drops an unreadable
  // record would lose evidence without saying so.
  const auto decode_failure = [](const StoredRecord& record, const Error& error) {
    return Error{error.code, "stored record could not be decoded",
                 std::string(to_string(record.type)) + " #" + std::to_string(record.sequence) +
                     ": " + error.message};
  };
  for (const auto& record : records) {
    switch (record.type) {
      case RecordType::Evidence: {
        Result<ObservedPathEvidence> evidence = codec::decode_exact<ObservedPathEvidence>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            config_.limits);
        if (!evidence.has_value()) {
          return decode_failure(record, evidence.error());
        }
        const Result<SubjectKey> key = resolve_subject(evidence.value());
        if (!key.has_value()) {
          continue;
        }
        auto& bucket = evidence_[key.value()];
        if (bucket.size() >= config_.limits.max_evidence_per_comparison) {
          continue;
        }
        const bool duplicate = std::any_of(
            bucket.begin(), bucket.end(), [&evidence](const ObservedPathEvidence& item) {
              return item.id == evidence.value().id;
            });
        if (!duplicate) {
          bucket.push_back(evidence.value());
        }
        break;
      }
      case RecordType::Comparison: {
        Result<ComparisonResult> comparison = codec::decode_exact<ComparisonResult>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            config_.limits);
        if (!comparison.has_value()) {
          return decode_failure(record, comparison.error());
        }
        const Status appended = history_.append(comparison.value());
        if (!appended.has_value()) {
          return appended.error();
        }
        break;
      }
      case RecordType::Finding: {
        Result<Finding> finding = codec::decode_exact<Finding>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            config_.limits);
        if (!finding.has_value()) {
          return decode_failure(record, finding.error());
        }
        const Status restored = findings_.restore(finding.value());
        if (!restored.has_value()) {
          return restored.error();
        }
        break;
      }
      case RecordType::Source: {
        Result<SourceDescriptor> descriptor = codec::decode_exact<SourceDescriptor>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            config_.limits);
        if (!descriptor.has_value()) {
          return decode_failure(record, descriptor.error());
        }
        const Status registered = sources_.register_source(std::move(descriptor).value());
        if (!registered.has_value()) {
          return registered.error();
        }
        break;
      }
      case RecordType::TopologyGeneration:
      case RecordType::RouteGeneration:
      case RecordType::Marker:
        // Expected state is never resurrected from disk.
        break;
    }
  }
  return ok();
}

Status ObservatoryRuntime::persist_evidence(const ObservedPathEvidence& evidence) {
  if (!store_.has_value()) {
    return ok();
  }
  const std::string payload = codec::encode(evidence);
  const Status appended = store_->append(
      RecordType::Evidence,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
  if (!appended.has_value()) {
    return appended.error();
  }
  metrics_.add_store_append();
  return ok();
}

Status ObservatoryRuntime::persist_comparison(const ComparisonResult& result) {
  if (!store_.has_value()) {
    return ok();
  }
  const std::string payload = codec::encode(result);
  const Status appended = store_->append(
      RecordType::Comparison,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
  if (!appended.has_value()) {
    return appended.error();
  }
  metrics_.add_store_append();
  return ok();
}

Status ObservatoryRuntime::register_source(SourceDescriptor descriptor) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (store_.has_value()) {
    const std::string payload = codec::encode(descriptor);
    const Status appended = store_->append(
        RecordType::Source, std::span<const std::byte>(
                                reinterpret_cast<const std::byte*>(payload.data()),
                                payload.size()));
    if (!appended.has_value()) {
      return appended.error();
    }
    metrics_.add_store_append();
  }
  return sources_.register_source(std::move(descriptor));
}

Result<SourceDescriptor> ObservatoryRuntime::source(SourceId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sources_.find(id);
}

std::vector<SourceDescriptor> ObservatoryRuntime::sources() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sources_.all();
}

Status ObservatoryRuntime::publish_topology(TopologyGeneration generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Status finalized = generation.finalize(config_.limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  if (!sources_.contains(generation.source)) {
    return Error{ErrorCode::UnknownIdentity,
                 "topology generation names a source that is not registered",
                 generation.source.str()};
  }
  if (epoch_.valid() && generation.epoch < epoch_) {
    return Error{ErrorCode::FencedEvidence,
                 "topology generation belongs to a superseded epoch", generation.epoch.str()};
  }
  // A revision is only comparable inside the epoch and incarnation that
  // produced it: a new epoch restarts the revision space, so an older epoch's
  // numbering must not fence a newer epoch's first publication.
  if (topology_.has_value() && topology_->epoch == generation.epoch &&
      topology_->incarnation == generation.incarnation &&
      generation.revision < topology_->revision) {
    return Error{ErrorCode::FencedEvidence, "topology generation is an older revision",
                 generation.revision.str()};
  }
  if (topology_.has_value() && topology_->id == generation.id &&
      topology_->revision == generation.revision) {
    // Idempotent republication of the same generation.
    return ok();
  }

  PATHOBS_TRY(index, TopologyIndex::build(generation, config_.limits));
  epoch_ = generation.epoch;
  incarnation_ = generation.incarnation;
  topology_revision_ = generation.revision;
  topology_index_ = std::move(index);
  topology_ = std::move(generation);
  if (store_.has_value()) {
    const std::string payload = codec::encode(*topology_);
    const Status appended = store_->append(
        RecordType::TopologyGeneration,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size()));
    if (!appended.has_value()) {
      return appended.error();
    }
    metrics_.add_store_append();
  }
  return ok();
}

Status ObservatoryRuntime::publish_routes(RouteGeneration generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Status finalized = generation.finalize(config_.limits);
  if (!finalized.has_value()) {
    return finalized.error();
  }
  if (!sources_.contains(generation.source)) {
    return Error{ErrorCode::UnknownIdentity,
                 "route generation names a source that is not registered", generation.source.str()};
  }
  if (!topology_.has_value()) {
    return Error{ErrorCode::InvalidState,
                 "route generation references topology generation " + generation.topology.str() +
                     " which has not been published"};
  }
  if (generation.topology != topology_->id) {
    return Error{ErrorCode::UnknownGeneration,
                 "route generation references a topology generation that is not current",
                 generation.topology.str()};
  }
  if (epoch_.valid() && generation.epoch < epoch_) {
    return Error{ErrorCode::FencedEvidence,
                 "route generation belongs to a superseded epoch", generation.epoch.str()};
  }
  if (routes_.has_value() && routes_->epoch == generation.epoch &&
      routes_->incarnation == generation.incarnation &&
      generation.revision < routes_->revision) {
    return Error{ErrorCode::FencedEvidence, "route generation is an older revision",
                 generation.revision.str()};
  }
  if (routes_.has_value() && routes_->id == generation.id &&
      routes_->revision == generation.revision) {
    return ok();
  }

  epoch_ = generation.epoch;
  incarnation_ = generation.incarnation;
  route_revision_ = generation.revision;
  routes_ = std::move(generation);
  if (store_.has_value()) {
    const std::string payload = codec::encode(*routes_);
    const Status appended = store_->append(
        RecordType::RouteGeneration,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size()));
    if (!appended.has_value()) {
      return appended.error();
    }
    metrics_.add_store_append();
  }
  return ok();
}

bool ObservatoryRuntime::has_topology() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return topology_.has_value();
}

bool ObservatoryRuntime::has_routes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return routes_.has_value();
}

AuthorityState ObservatoryRuntime::authority_state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  AuthorityState state;
  state.epoch = epoch_;
  state.incarnation = incarnation_;
  state.route_revision = route_revision_;
  state.topology_revision = topology_revision_;
  if (routes_.has_value()) {
    state.route_source = routes_->source;
  }
  if (topology_.has_value()) {
    state.topology_source = topology_->source;
  }
  return state;
}

Result<TopologyGeneration> ObservatoryRuntime::topology() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!topology_.has_value()) {
    return Error{ErrorCode::UnknownGeneration, "no topology generation is current"};
  }
  return *topology_;
}

Result<RouteGeneration> ObservatoryRuntime::routes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!routes_.has_value()) {
    return Error{ErrorCode::UnknownGeneration, "no route generation is current"};
  }
  return *routes_;
}

Result<SubjectKey> ObservatoryRuntime::resolve_subject(const ObservedPathEvidence& evidence) const {
  SubjectKey key;
  if (evidence.route_generation.present()) {
    key.generation = evidence.route_generation.value();
  } else if (routes_.has_value()) {
    key.generation = routes_->id;
  } else {
    return Error{ErrorCode::UnknownGeneration,
                 "evidence names no route generation and no generation is current"};
  }

  if (evidence.group.present()) {
    key.group = evidence.group;
    return key;
  }
  if (!routes_.has_value() || routes_->id != key.generation) {
    return key;
  }
  if (evidence.path.present()) {
    const ExpectedPath* path = routes_->find_path(evidence.path.value());
    if (path != nullptr) {
      key.group = MaybeId<PathGroupId>(path->group);
      return key;
    }
  }
  if (evidence.origin.present() && evidence.destination.present()) {
    bool ambiguous = false;
    const PathGroup* group =
        routes_->find_group_by_endpoints(evidence.origin.value(), evidence.destination.value(),
                                         &ambiguous);
    if (group != nullptr && !ambiguous) {
      key.group = MaybeId<PathGroupId>(group->id);
    }
  }
  return key;
}

Result<IngestResult> ObservatoryRuntime::ingest_evidence(ObservedPathEvidence evidence) {
  std::lock_guard<std::mutex> lock(mutex_);
  IngestResult result;

  const Status shape = evidence.validate(config_.limits);
  if (!shape.has_value()) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    result.reason = DivergenceClass::EvidenceUnknown;
    result.detail = shape.error().message;
    return result;
  }

  const Result<SourceDescriptor> descriptor = sources_.find(evidence.source);
  if (!descriptor.has_value()) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    metrics_.add_evidence_unregistered_source(1);
    result.reason = DivergenceClass::AuthorityInsufficient;
    result.detail = "evidence was produced by an unregistered source";
    return result;
  }
  if (evidence.authority > descriptor.value().authority) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    result.reason = DivergenceClass::AuthorityInsufficient;
    result.detail = "evidence claims more authority than its source is registered with";
    return result;
  }
  if (descriptor.value().surface != evidence.surface) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    result.reason = DivergenceClass::EvidenceUnknown;
    result.detail = "evidence proof surface does not match the registered source";
    return result;
  }

  const SourceSequenceLedger::Outcome sequence =
      ledger_.observe(evidence.source, evidence.incarnation, evidence.sequence);
  if (sequence.verdict == SourceSequenceLedger::Verdict::Replay) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    metrics_.add_evidence_replayed(1);
    result.reason = DivergenceClass::SourceSequenceReplay;
    result.replay = true;
    result.detail = "source sequence has already been accepted for this incarnation";
    return result;
  }
  if (sequence.verdict == SourceSequenceLedger::Verdict::Gap) {
    metrics_.add_evidence_gap_events(1);
    source_has_gap_[evidence.source] = true;
    result.sequence_gap = true;
  } else if (sequence.verdict == SourceSequenceLedger::Verdict::NewIncarnation) {
    // A new incarnation clears the gap flag: the missing range belonged to the
    // incarnation that ended.
    source_has_gap_[evidence.source] = false;
  }

  evidence.seal();
  result.id = evidence.id;

  const Result<SubjectKey> key = resolve_subject(evidence);
  if (!key.has_value()) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    result.reason = DivergenceClass::EvidenceUnknown;
    result.detail = key.error().message;
    return result;
  }

  auto& bucket = evidence_[key.value()];
  if (bucket.size() >= config_.limits.max_evidence_per_comparison) {
    metrics_.add_evidence_offered(1);
    metrics_.add_evidence_rejected(1);
    result.reason = DivergenceClass::EvidenceIncomplete;
    result.detail = "evidence bucket has reached the configured bound";
    return result;
  }
  for (const auto& existing : bucket) {
    if (existing.id == evidence.id) {
      // The same observation delivered twice is one observation.
      metrics_.add_evidence_offered(1);
      result.accepted = true;
      result.detail = "duplicate evidence identity: already recorded";
      return result;
    }
  }

  const Status persisted = persist_evidence(evidence);
  if (!persisted.has_value()) {
    result.reason = DivergenceClass::EvidenceUnknown;
    result.detail = persisted.error().message;
    return result;
  }
  result.persisted = persisted.has_value();

  bucket.push_back(std::move(evidence));
  metrics_.add_evidence_offered(1);
  metrics_.add_evidence_accepted(1);
  result.accepted = true;
  return result;
}

Result<std::size_t> ObservatoryRuntime::ingest_batch(
    const std::vector<ObservedPathEvidence>& batch) {
  if (batch.size() > config_.limits.max_evidence_per_ingest_batch) {
    return Error{ErrorCode::CapacityExceeded, "batch exceeds the configured ingest bound"};
  }
  std::size_t accepted = 0;
  for (const auto& evidence : batch) {
    const Result<IngestResult> result = ingest_evidence(evidence);
    if (!result.has_value()) {
      return result.error();
    }
    if (result.value().accepted) {
      ++accepted;
    }
  }
  return accepted;
}

std::size_t ObservatoryRuntime::pending_batches() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pool_ == nullptr ? 0 : pool_->pending();
}

Status ObservatoryRuntime::submit_batch(std::vector<ObservedPathEvidence> batch) {
  WorkerPool* pool = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != RuntimeState::Running) {
      return Error{ErrorCode::InvalidState, "runtime is not running"};
    }
    pool = pool_.get();
    if (pool_ != nullptr && pool_->pending() >= kMaxPendingBatches) {
      return Error{ErrorCode::CapacityExceeded, "too many batches are already queued"};
    }
  }
  if (pool == nullptr) {
    return Error{ErrorCode::InvalidState, "runtime has no worker pool"};
  }
  return pool->submit([this, batch = std::move(batch)](const CancellationToken& token) {
    if (token.cancelled()) {
      metrics_.add_task_cancelled();
      return;
    }
    const Result<std::size_t> ingested = ingest_batch(batch);
    static_cast<void>(ingested);
    if (token.cancelled()) {
      metrics_.add_task_cancelled();
      return;
    }
    const Result<std::vector<ComparisonResult>> results = compare_all();
    static_cast<void>(results);
    metrics_.add_task_completed();
  });
}

Status ObservatoryRuntime::drain() {
  // The lock is taken only to read the pool pointer. Holding it while waiting
  // would deadlock against the worker, which needs the same lock to ingest.
  WorkerPool* pool = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pool = pool_.get();
  }
  if (pool == nullptr) {
    return ok();
  }
  for (;;) {
    const std::size_t outstanding = pool->pending() + pool->active();
    if (outstanding == 0) {
      break;
    }
    std::this_thread::yield();
  }
  return ok();
}

ComparisonContext ObservatoryRuntime::make_context() const {
  ComparisonContext context;
  context.routes = routes_.has_value() ? &*routes_ : nullptr;
  context.topology = topology_.has_value() ? &*topology_ : nullptr;
  context.topology_index = topology_index_.has_value() ? &*topology_index_ : nullptr;
  context.sources = &sources_;
  context.authority = AuthorityState{};
  context.authority.epoch = epoch_;
  context.authority.incarnation = incarnation_;
  context.authority.route_revision = route_revision_;
  context.authority.topology_revision = topology_revision_;
  context.now = now();
  return context;
}

Result<ComparisonResult> ObservatoryRuntime::compare_bucket(const SubjectKey& key, bool record) {
  ComparisonRequest request;
  request.generation = key.generation;
  request.group = key.group;
  const auto it = evidence_.find(key);
  if (it != evidence_.end()) {
    request.evidence = it->second;
    for (const auto& item : it->second) {
      const auto gap = source_has_gap_.find(item.source);
      if (gap != source_has_gap_.end() && gap->second) {
        request.source_sequence_gap = true;
      }
    }
  }

  const ComparisonEngine engine(config_.limits, config_.comparison);
  const Result<ComparisonResult> compared = engine.compare(request, make_context());
  if (!compared.has_value()) {
    return compared.error();
  }
  ComparisonResult result = compared.value();
  metrics_.add_comparison(result.outcome);

  if (!record) {
    return result;
  }

  const Status appended = history_.append(result);
  if (!appended.has_value()) {
    return appended.error();
  }
  const Result<FindingId> finding = findings_.record(result, now());
  if (!finding.has_value()) {
    return finding.error();
  }
  if (finding.value().valid()) {
    metrics_.add_finding_recorded();
  } else {
    const Result<std::size_t> resolved = findings_.resolve_from_match(result, now());
    if (!resolved.has_value()) {
      return resolved.error();
    }
    metrics_.add_finding_resolved(resolved.value());
  }
  if (store_.has_value()) {
    const Status persisted = persist_comparison(result);
    if (!persisted.has_value()) {
      return persisted.error();
    }
    const std::vector<Finding> all = findings_.all();
    for (const auto& item : all) {
      if (item.last_comparison == result.id) {
        const std::string payload = codec::encode(item);
        const Status appended_finding = store_->append(
            RecordType::Finding,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                       payload.size()));
        if (!appended_finding.has_value()) {
          return appended_finding.error();
        }
        metrics_.add_store_append();
      }
    }
  }
  return result;
}

Result<ComparisonResult> ObservatoryRuntime::compare_group(PathGroupId group) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : evidence_) {
    if (entry.first.group.present() && entry.first.group.value() == group) {
      return compare_bucket(entry.first, true);
    }
  }
  return Error{ErrorCode::UnknownIdentity, "no evidence is held for this path group", group.str()};
}

Result<ComparisonResult> ObservatoryRuntime::compare_path(PathId path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!routes_.has_value()) {
    return Error{ErrorCode::UnknownGeneration, "no route generation is current"};
  }
  const ExpectedPath* expected = routes_->find_path(path);
  if (expected == nullptr) {
    return Error{ErrorCode::UnknownIdentity, "no expected path with this identity", path.str()};
  }
  for (const auto& entry : evidence_) {
    if (entry.first.group.present() && entry.first.group.value() == expected->group) {
      return compare_bucket(entry.first, true);
    }
  }
  return Error{ErrorCode::UnknownIdentity, "no evidence is held for this path", path.str()};
}

Result<std::vector<ComparisonResult>> ObservatoryRuntime::compare_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ComparisonResult> results;
  results.reserve(evidence_.size());
  for (const auto& entry : evidence_) {
    PATHOBS_TRY(result, compare_bucket(entry.first, true));
    results.push_back(std::move(result));
  }
  return results;
}

Result<HistoryQueryResult> ObservatoryRuntime::history(const HistoryQuery& request) const {
  std::lock_guard<std::mutex> lock(mutex_);
  HistoryQuery effective = request;
  if (!effective.now.is_set()) {
    effective.now = clock_->now();
    effective.apply_window = true;
  }
  return history_.query(effective);
}

std::vector<Finding> ObservatoryRuntime::findings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findings_.all();
}

Result<Finding> ObservatoryRuntime::finding(FindingId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findings_.find(id);
}

Status ObservatoryRuntime::acknowledge_finding(FindingId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return findings_.acknowledge(id, now());
}

Result<std::size_t> ObservatoryRuntime::prune_history() {
  std::lock_guard<std::mutex> lock(mutex_);
  return history_.prune(now());
}

Status ObservatoryRuntime::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!store_.has_value()) {
    return ok();
  }
  const std::vector<Finding> all = findings_.all();
  for (const auto& item : all) {
    const std::string payload = codec::encode(item);
    const Status appended = store_->append(
        RecordType::Finding,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size()));
    if (!appended.has_value()) {
      return appended.error();
    }
    metrics_.add_store_append();
  }
  return ok();
}

bool ObservatoryRuntime::persistent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return store_.has_value();
}

RecoveryReport ObservatoryRuntime::recovery() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return store_.has_value() ? store_->recovery() : RecoveryReport{};
}

RuntimeStats ObservatoryRuntime::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RuntimeStats stats;
  stats.state = state_;
  stats.metrics = metrics_.snapshot();
  stats.evidence_buckets = evidence_.size();
  for (const auto& entry : evidence_) {
    stats.evidence_records += entry.second.size();
  }
  stats.findings = findings_.size();
  stats.findings_open = findings_.open_count();
  stats.history = history_.size();
  stats.queue_pending = pool_ == nullptr ? 0 : pool_->pending();
  stats.epoch = epoch_;
  stats.incarnation = incarnation_;
  stats.route_revision = route_revision_;
  stats.has_route_generation = routes_.has_value();
  stats.has_topology_generation = topology_.has_value();
  if (routes_.has_value()) {
    stats.route_generation = routes_->id;
    stats.topology_generation = routes_->topology;
  } else if (topology_.has_value()) {
    stats.topology_generation = topology_->id;
  }
  if (store_.has_value()) {
    stats.recovery = store_->recovery();
  }
  return stats;
}

} // namespace pathobs
