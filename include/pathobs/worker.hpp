// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Bounded worker pool with real cancellation.
//
//   * the queue is bounded; a submission that does not fit is rejected with a
//     stable error code rather than growing without limit;
//   * every task receives a cancellation token it is expected to observe at
//     bounded checkpoints;
//   * cancel_pending() drops the tasks that have not started and reports how
//     many were dropped;
//   * shutdown() joins every worker. It must not be called from a worker
//     thread; the pool detects that case and reports it instead of deadlocking.

#ifndef PATHOBS_WORKER_HPP
#define PATHOBS_WORKER_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace pathobs {

class PATHOBS_API CancellationToken {
 public:
  CancellationToken() = default;

  bool cancelled() const noexcept {
    return flag_ != nullptr && flag_->load(std::memory_order_acquire);
  }

 private:
  friend class CancellationSource;
  explicit CancellationToken(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}
  std::shared_ptr<std::atomic<bool>> flag_{};
};

class PATHOBS_API CancellationSource {
 public:
  CancellationSource() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

  CancellationToken token() const { return CancellationToken(flag_); }
  void cancel() noexcept { flag_->store(true, std::memory_order_release); }
  bool cancelled() const noexcept { return flag_->load(std::memory_order_acquire); }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
};

using Task = std::function<void(const CancellationToken&)>;

class PATHOBS_API WorkerPool {
 public:
  WorkerPool(std::size_t threads, std::size_t max_queue);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  Status start();
  Status submit(Task task);

  /// Cancel and drop every task that has not started. Returns how many were
  /// dropped. A task that is already running keeps its token: cancelling work
  /// in flight is what shutdown(true) is for.
  std::size_t cancel_pending();

  /// Stop the pool and join every worker. When cancel_running is set, every
  /// outstanding token is cancelled first so tasks observing it stop early.
  void shutdown(bool cancel_running = false);

  std::size_t pending() const;
  std::size_t active() const;
  std::size_t threads() const noexcept { return thread_count_; }
  std::uint64_t completed() const noexcept { return completed_.load(std::memory_order_relaxed); }
  std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
  bool running() const noexcept { return running_.load(std::memory_order_acquire); }

 private:
  struct Entry {
    Task task;
    std::shared_ptr<CancellationSource> source;
  };

  void worker_loop(std::size_t index);
  void compact_outstanding_locked();

  std::size_t thread_count_;
  std::size_t max_queue_;
  mutable std::mutex mutex_;
  std::condition_variable work_available_;
  std::condition_variable idle_;
  std::deque<Entry> queue_;
  std::vector<std::weak_ptr<CancellationSource>> outstanding_;
  std::vector<std::thread> threads_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> completed_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::size_t active_{0};
  bool accepting_{false};
};

} // namespace pathobs

#endif // PATHOBS_WORKER_HPP
