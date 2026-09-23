# Concurrency, locking and cancellation

## Lock ownership and order

    ObservatoryRuntime::mutex_
      -> SourceRegistry::mutex_
      -> SourceSequenceLedger::mutex_
      -> HistoryStore::mutex_
      -> FindingRegistry::mutex_
      -> WorkerPool::mutex_

The order is one-way. The rules that keep it that way:

1. No component calls back into the runtime while holding its own lock. The
   registries, the ledger, the history and the findings registry are leaves: their
   methods take their own lock, touch only their own state, and return values.
2. The runtime lock is never acquired while holding a leaf lock.
3. Persistence is performed under the runtime lock but never calls back into the
   runtime. The store owns its own handle and does not lock.
4. `ObservatoryRuntime::drain()` reads the pool pointer under the runtime lock,
   releases it, and only then waits. Holding the lock while waiting would deadlock
   against a worker that needs the same lock to ingest — which is exactly the bug
   the original implementation had, and why the wait is written the way it is.

## Ownership of mutable state

| State | Owner | Guard |
| --- | --- | --- |
| Sources | `SourceRegistry` | its own mutex |
| Sequence high water marks | `SourceSequenceLedger` | its own mutex |
| Current topology, index, routes, evidence buckets | `ObservatoryRuntime` | the runtime mutex |
| History | `HistoryStore` | its own mutex |
| Findings | `FindingRegistry` | its own mutex |
| Worker queue | `WorkerPool` | its own mutex |
| Store file | one `PersistentStore` instance | single owner by convention; a second open of the same path is refused by the operating system, and the store reports that rather than corrupting the file |
| Metrics | `Metrics` | relaxed atomics; totals only |

`PersistentStore::read_all` reads through the store's own handle, so it is not
safe to call it concurrently with `append`. Callers serialise; the concurrency
test does exactly that with an explicit mutex and asserts that nothing is lost.

## Cancellation

`WorkerPool::submit` gives each task a token from its own `CancellationSource`.

* `cancel_pending()` cancels the tokens of every queued task and clears the
  queue, returning how many were dropped.
* `shutdown(false)` stops accepting, drains the queue and joins every worker.
* `shutdown(true)` cancels every outstanding token first, so a task that is
  already running observes cancellation at its next checkpoint and returns.
* Submitting after shutdown returns `ShuttingDown` rather than silently
  dropping the task.

A cancelled task is expected to check its token at bounded checkpoints. The
runtime's own tasks do so before and after their work.

## Liveness without timeouts

The test suite contains no timeouts. Waits are on conditions the runtime is
required to satisfy:

* the worker bound test spins until the occupying task has set a "running" flag
  before it fills the queue;
* the reader test spins until each reader has completed a pass before it stops
  them;
* the cancellation test spins until the task has started, then cancels;
* `drain()` returns only when nothing is queued or running.

If any of those conditions is never satisfied the suite hangs. That is the
intended signal: a liveness defect must not be laundered into a flaky pass.
