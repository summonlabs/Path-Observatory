// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Concurrency, cancellation and lock ownership.
//
// The lock order in the runtime is:
//   ObservatoryRuntime::mutex_  ->  SourceRegistry::mutex_
//                               ->  SourceSequenceLedger::mutex_
//                               ->  HistoryStore::mutex_
//                               ->  FindingRegistry::mutex_
// No component ever calls back into the runtime while holding its own lock, and
// no component acquires the runtime lock while holding one of its own. The
// tests below exercise the paths that would deadlock if that order were broken.
//
// No test here uses a timeout. Synchronisation is by condition the runtime is
// required to satisfy; a liveness defect shows up as a hang, which is the
// honest signal.

#include "test_framework.hpp"

#include "pathobs/persistence.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/worker.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace pathobs;

namespace {

std::filesystem::path scratch_path(const char* name) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "pathobs-tests";
  std::error_code ignored;
  std::filesystem::create_directories(directory, ignored);
  const std::filesystem::path path = directory / name;
  std::filesystem::remove(path, ignored);
  return path;
}

TimePoint base_time() { return TimePoint::from_unix_nanos(1767225600000000000LL); }

} // namespace

PATHOBS_TEST(workers, every_submitted_task_completes) {
  WorkerPool pool(4, 1024);
  REQUIRE(pool.start().has_value());
  std::atomic<std::uint64_t> executed{0};
  constexpr std::uint64_t kTasks = 500;
  for (std::uint64_t i = 0; i < kTasks; ++i) {
    const Status submitted = pool.submit([&executed](const CancellationToken& token) {
      if (token.cancelled()) {
        return;
      }
      executed.fetch_add(1, std::memory_order_relaxed);
    });
    REQUIRE(submitted.has_value());
  }
  pool.shutdown(false);
  CHECK_EQ(executed.load(std::memory_order_relaxed), kTasks);
  CHECK_EQ(pool.completed(), kTasks);
  CHECK_EQ(pool.pending(), 0u);
  CHECK_EQ(pool.active(), 0u);
}

PATHOBS_TEST(workers, the_queue_bound_is_enforced) {
  WorkerPool pool(1, 4);
  REQUIRE(pool.start().has_value());
  std::atomic<bool> release{false};
  std::atomic<bool> occupying{false};
  // Occupy the single worker so the queue cannot drain. The test waits until
  // the occupying task is genuinely running before it fills the queue; there is
  // no timeout, the condition is one the pool must satisfy.
  REQUIRE(pool.submit([&release, &occupying](const CancellationToken&) {
            occupying.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) {
              std::this_thread::yield();
            }
          }).has_value());
  while (!occupying.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::size_t accepted = 0;
  std::size_t rejected = 0;
  for (int i = 0; i < 64; ++i) {
    const Status submitted = pool.submit([](const CancellationToken&) {});
    if (submitted.has_value()) {
      ++accepted;
    } else {
      CHECK_EQ(static_cast<int>(submitted.error().code),
               static_cast<int>(ErrorCode::CapacityExceeded));
      ++rejected;
    }
  }
  CHECK(rejected > 0);
  CHECK(accepted <= 4);
  CHECK_EQ(pool.dropped(), static_cast<std::uint64_t>(rejected));

  const std::size_t cancelled = pool.cancel_pending();
  CHECK_EQ(cancelled, accepted);
  release.store(true, std::memory_order_release);
  pool.shutdown(false);
  CHECK_EQ(pool.pending(), 0u);
}

PATHOBS_TEST(workers, submit_after_shutdown_is_refused) {
  WorkerPool pool(2, 8);
  REQUIRE(pool.start().has_value());
  pool.shutdown(false);
  const Status submitted = pool.submit([](const CancellationToken&) {});
  CHECK(!submitted.has_value());
  CHECK_EQ(static_cast<int>(submitted.error().code), static_cast<int>(ErrorCode::ShuttingDown));
  CHECK(!pool.running());
}

PATHOBS_TEST(workers, a_running_task_observes_cancellation) {
  WorkerPool pool(1, 8);
  REQUIRE(pool.start().has_value());
  std::atomic<bool> started{false};
  std::atomic<bool> observed{false};
  REQUIRE(pool.submit([&started, &observed](const CancellationToken& token) {
            started.store(true, std::memory_order_release);
            while (!token.cancelled()) {
              std::this_thread::yield();
            }
            observed.store(true, std::memory_order_release);
          }).has_value());
  // Spin until the task is genuinely running, then cancel it. There is no
  // timeout: the task is guaranteed to start because the pool has a worker and
  // an empty queue.
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  pool.shutdown(true);
  CHECK(observed.load(std::memory_order_acquire));
}

PATHOBS_TEST(workers, an_empty_task_is_refused) {
  WorkerPool pool(2, 8);
  REQUIRE(pool.start().has_value());
  const Status submitted = pool.submit(Task{});
  CHECK(!submitted.has_value());
  pool.shutdown(false);
}

PATHOBS_TEST(concurrency, concurrent_ingest_and_compare_is_consistent) {
  const Limits limits = default_limits();

  // The plan comes from one fabric; the observations come from four independent
  // observers, each with its own sequence space. Sharing one sequence space
  // across racing threads would make the test measure the ledger's replay fence
  // rather than the concurrency of the pipeline.
  synthetic::LabOptions base_options;
  base_options.evidence_count = 0;
  const auto base = synthetic::build_lab_fabric(base_options, limits);
  REQUIRE(base.has_value());

  std::vector<synthetic::LabFabric> observers;
  for (int index = 0; index < 4; ++index) {
    synthetic::LabOptions observer_options;
    observer_options.observer_source_id = 100 + static_cast<std::uint64_t>(index);
    observer_options.evidence_count = 4;
    const auto built = synthetic::build_lab_fabric(observer_options, limits);
    REQUIRE(built.has_value());
    observers.push_back(built.value());
  }

  RuntimeConfig config;
  config.limits = limits;
  config.worker_threads = 4;
  auto clock = std::make_unique<ManualClock>(base.value().routes.received);
  auto created = ObservatoryRuntime::create(config, std::move(clock));
  REQUIRE(created.has_value());
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  REQUIRE(runtime->start().has_value());
  for (const auto& descriptor : base.value().sources) {
    REQUIRE(runtime->register_source(descriptor).has_value());
  }
  for (const auto& observer : observers) {
    REQUIRE(runtime->register_source(observer.sources[1]).has_value());
  }
  REQUIRE(runtime->publish_topology(base.value().topology).has_value());
  REQUIRE(runtime->publish_routes(base.value().routes).has_value());

  std::atomic<std::uint64_t> accepted{0};
  std::atomic<std::uint64_t> refused{0};
  std::vector<std::thread> ingest_threads;
  for (int worker = 0; worker < 4; ++worker) {
    ingest_threads.emplace_back([&, worker]() {
      const std::vector<ObservedPathEvidence>& evidence =
          observers[static_cast<std::size_t>(worker)].evidence;
      for (const auto& record : evidence) {
        const auto ingested = runtime->ingest_evidence(record);
        if (!ingested.has_value() || !ingested.value().accepted) {
          refused.fetch_add(1, std::memory_order_relaxed);
        } else {
          accepted.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  std::atomic<std::uint64_t> comparisons{0};
  std::vector<std::string> digests(4);
  std::vector<std::thread> compare_threads;
  for (int worker = 0; worker < 4; ++worker) {
    compare_threads.emplace_back([&, worker]() {
      const auto results = runtime->compare_all();
      if (!results.has_value()) {
        return;
      }
      comparisons.fetch_add(results.value().size(), std::memory_order_relaxed);
      std::string combined;
      for (const auto& result : results.value()) {
        combined += result.canonical_digest().hex();
      }
      digests[static_cast<std::size_t>(worker)] = std::move(combined);
    });
  }
  for (auto& thread : ingest_threads) {
    thread.join();
  }
  for (auto& thread : compare_threads) {
    thread.join();
  }

  // Every record is accepted exactly once even though four threads raced.
  CHECK_EQ(accepted.load(std::memory_order_relaxed), 16ull);
  CHECK_EQ(refused.load(std::memory_order_relaxed), 0ull);
  CHECK(comparisons.load(std::memory_order_relaxed) > 0ull);

  // Re-offering every record concurrently changes nothing: the ledger fences the
  // replays and the bucket keeps one copy of each identity.
  std::vector<std::thread> replay_threads;
  std::atomic<std::uint64_t> replayed{0};
  for (int worker = 0; worker < 4; ++worker) {
    replay_threads.emplace_back([&, worker]() {
      for (const auto& record : observers[static_cast<std::size_t>(worker)].evidence) {
        const auto ingested = runtime->ingest_evidence(record);
        if (!ingested.has_value() || !ingested.value().accepted) {
          replayed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& thread : replay_threads) {
    thread.join();
  }
  CHECK_EQ(replayed.load(std::memory_order_relaxed), 16ull);
  CHECK_EQ(runtime->stats().evidence_records, 16u);

  // The final state is the evidence bucket, and it is complete.
  const RuntimeStats stats = runtime->stats();
  CHECK_EQ(stats.evidence_records, 16u);
  CHECK_EQ(stats.evidence_buckets, 1u);

  // Comparing once more produces the same classification as any racing reader.
  const auto settled = runtime->compare_all();
  REQUIRE(settled.has_value());
  REQUIRE(settled.value().size() == 1u);
  CHECK_EQ(settled.value().front().outcome, MatchOutcome::Match);
  CHECK_EQ(settled.value().front().completeness, EvidenceCompleteness::Complete);

  // History and findings remain queryable after the storm.
  const auto history = runtime->history({});
  REQUIRE(history.has_value());
  CHECK(history.value().records.size() >= 1u);
  static_cast<void>(runtime->findings());
  CHECK(runtime->stop().has_value());
}

PATHOBS_TEST(concurrency, concurrent_readers_do_not_block_the_pipeline) {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 4;
  const auto fabric = synthetic::build_lab_fabric(lab, limits);
  REQUIRE(fabric.has_value());

  RuntimeConfig config;
  config.limits = limits;
  auto clock = std::make_unique<ManualClock>(fabric.value().routes.received);
  auto created = ObservatoryRuntime::create(config, std::move(clock));
  REQUIRE(created.has_value());
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  REQUIRE(runtime->start().has_value());
  for (const auto& descriptor : fabric.value().sources) {
    REQUIRE(runtime->register_source(descriptor).has_value());
  }
  REQUIRE(runtime->publish_topology(fabric.value().topology).has_value());
  REQUIRE(runtime->publish_routes(fabric.value().routes).has_value());
  for (const auto& evidence : fabric.value().evidence) {
    static_cast<void>(runtime->ingest_evidence(evidence));
  }

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> reads{0};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&]() {
      while (!stop.load(std::memory_order_acquire)) {
        static_cast<void>(runtime->stats());
        static_cast<void>(runtime->history({}));
        static_cast<void>(runtime->findings());
        reads.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::uint64_t comparisons = 0;
  for (int round = 0; round < 64; ++round) {
    const auto results = runtime->compare_all();
    REQUIRE(results.has_value());
    comparisons += results.value().size();
    std::this_thread::yield();
  }
  // The readers must each have completed at least one pass before the run ends;
  // otherwise "no deadlock" would be indistinguishable from "never ran".
  while (reads.load(std::memory_order_relaxed) < 4) {
    std::this_thread::yield();
  }
  stop.store(true, std::memory_order_release);
  for (auto& thread : readers) {
    thread.join();
  }
  CHECK(comparisons > 0);
  CHECK(reads.load(std::memory_order_relaxed) >= 4);
  CHECK(runtime->stop().has_value());
}

PATHOBS_TEST(concurrency, concurrent_store_appends_are_all_durable) {
  const std::filesystem::path path = scratch_path("concurrent.pob");
  StoreOptions options;

  // The store is not documented as thread safe for concurrent appends, so the
  // test drives it the way a caller must: one writer at a time. What is proven
  // here is that a serialised multi-threaded writer loses nothing.
  std::mutex writer;
  std::vector<std::thread> threads;
  constexpr int kThreads = 4;
  constexpr int kPerThread = 32;
  std::atomic<int> failures{0};
  {
    // One store instance owns the file for the duration of the write storm.
    auto store = PersistentStore::open(path, options, base_time());
    REQUIRE(store.has_value());
    PersistentStore& shared = store.value();
    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back([&shared, &writer, &failures, i]() {
        for (int j = 0; j < kPerThread; ++j) {
          const std::string payload = "t" + std::to_string(i) + "-" + std::to_string(j);
          std::lock_guard<std::mutex> lock(writer);
          const Status appended = shared.append(
              RecordType::Marker,
              std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                         payload.size()));
          if (!appended.has_value()) {
            failures.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
    for (auto& thread : threads) {
      thread.join();
    }
    CHECK_EQ(failures.load(std::memory_order_relaxed), 0);
    CHECK_EQ(shared.record_count(), static_cast<std::uint64_t>(kThreads * kPerThread));
    CHECK(shared.mark_clean(base_time()).has_value());
  }

  // A restart reads every record back, in order, with its sequence intact.
  auto reopened = PersistentStore::open(path, options, base_time());
  REQUIRE(reopened.has_value());
  const auto records = reopened.value().read_all();
  REQUIRE(records.has_value());
  CHECK_EQ(records.value().size(), static_cast<std::size_t>(kThreads * kPerThread));
  std::uint64_t expected_sequence = 1;
  for (const auto& record : records.value()) {
    CHECK_EQ(record.sequence, expected_sequence);
    ++expected_sequence;
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}
