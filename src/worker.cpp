// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/worker.hpp"

namespace pathobs {

WorkerPool::WorkerPool(std::size_t threads, std::size_t max_queue)
    : thread_count_(threads), max_queue_(max_queue) {}

WorkerPool::~WorkerPool() { shutdown(false); }

Status WorkerPool::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_.load(std::memory_order_acquire)) {
    return Error{ErrorCode::InvalidState, "worker pool is already running"};
  }
  if (thread_count_ == 0) {
    return Error{ErrorCode::InvalidArgument, "worker pool needs at least one thread"};
  }
  accepting_ = true;
  running_.store(true, std::memory_order_release);
  threads_.clear();
  threads_.reserve(thread_count_);
  for (std::size_t i = 0; i < thread_count_; ++i) {
    threads_.emplace_back([this, i]() { worker_loop(i); });
  }
  return ok();
}

Status WorkerPool::submit(Task task) {
  if (!task) {
    return Error{ErrorCode::InvalidArgument, "worker pool received an empty task"};
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load(std::memory_order_acquire) || !accepting_) {
      return Error{ErrorCode::ShuttingDown, "worker pool is not accepting work"};
    }
    if (queue_.size() >= max_queue_) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return Error{ErrorCode::CapacityExceeded, "worker pool queue is full"};
    }
    auto source = std::make_shared<CancellationSource>();
    queue_.push_back(Entry{std::move(task), source});
    outstanding_.push_back(source);
    compact_outstanding_locked();
  }
  work_available_.notify_one();
  return ok();
}

void WorkerPool::compact_outstanding_locked() {
  if (outstanding_.size() <= max_queue_ * 2 + 64) {
    return;
  }
  std::vector<std::weak_ptr<CancellationSource>> kept;
  kept.reserve(outstanding_.size());
  for (auto& entry : outstanding_) {
    if (!entry.expired()) {
      kept.push_back(entry);
    }
  }
  outstanding_ = std::move(kept);
}

std::size_t WorkerPool::cancel_pending() {
  std::size_t cancelled = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled = queue_.size();
    for (auto& entry : queue_) {
      entry.source->cancel();
    }
    queue_.clear();
  }
  return cancelled;
}

void WorkerPool::shutdown(bool cancel_running) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load(std::memory_order_acquire) && threads_.empty()) {
      return;
    }
    if (cancel_running) {
      for (auto& entry : outstanding_) {
        if (auto source = entry.lock()) {
          source->cancel();
        }
      }
      for (auto& entry : queue_) {
        entry.source->cancel();
      }
    }
    accepting_ = false;
    running_.store(false, std::memory_order_release);
  }
  work_available_.notify_all();
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    outstanding_.clear();
  }
}

void WorkerPool::worker_loop(std::size_t index) {
  static_cast<void>(index);
  for (;;) {
    Entry entry;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      work_available_.wait(lock, [this]() {
        return !queue_.empty() || !running_.load(std::memory_order_acquire);
      });
      if (!running_.load(std::memory_order_acquire) && queue_.empty()) {
        return;
      }
      if (queue_.empty()) {
        continue;
      }
      entry = std::move(queue_.front());
      queue_.pop_front();
      ++active_;
    }

    const CancellationToken token = entry.source->token();
    if (!token.cancelled()) {
      entry.task(token);
    }
    completed_.fetch_add(1, std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      --active_;
    }
    idle_.notify_all();
  }
}

std::size_t WorkerPool::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

std::size_t WorkerPool::active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

} // namespace pathobs
