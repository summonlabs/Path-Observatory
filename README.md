# Path Observatory

Path Observatory is a vendor neutral C++20 runtime that owns the observation of
**actual path behaviour versus expected path behaviour** in a fabric.

It consumes expected path state from a routing or planning authority, consumes
observed path evidence from observation sources, and reports whether the two
agree — with the evidence's generation, epoch, incarnation, revision, freshness,
source authority and proof surface stated explicitly for every conclusion.

It does not compute routes, choose alternates, mutate forwarding, own blackhole
or loop containment, or treat path legality as proof of delivery.

Summon Software Labs. Apache License 2.0. No telemetry transmission.

## What is implemented

| Capability | Where |
| --- | --- |
| Typed identities for path, hop, device, port, link, source, route generation, topology generation, epoch, incarnation, revision and source sequence | `include/pathobs/strong_id.hpp` |
| Expected path model: route generations, path groups, multipath membership | `include/pathobs/expected.hpp` |
| Observed path model: evidence records, hop traces, link provenance, completeness | `include/pathobs/observed.hpp` |
| Observation classification: complete, partial, ambiguous, unknown, conflicting, unsupported | `src/compare.cpp` |
| Deterministic divergence classes with stable codes and a fixed precedence | `include/pathobs/divergence.hpp` |
| Actual-versus-expected comparison with provenance and a deterministic rationale | `src/compare.cpp` |
| Stable finding identities that survive reordering and duplication | `src/finding.cpp` |
| Bounded comparison history with a count, per-subject and time window | `src/history.cpp` |
| Topology and link correlation, conservative about ambiguity | `src/topology.cpp` |
| Legitimate multipath handling (an alternate member is not divergence) | `src/compare.cpp` |
| Freshness aware status, evaluated against an injected time | `src/freshness.cpp` |
| Versioned, integrity checked persistence with conservative recovery | `src/persistence.cpp` |
| Bounded worker pipeline with real cancellation and shutdown | `src/worker.cpp` |
| Real loopback TCP transport and an independent observation process | `src/transport.cpp`, `src/socket.cpp`, `tools/pathobs_node_main.cpp` |
| Ingest, compare, history, explain, export and validate tooling | `tools/pathobs_main.cpp` |
| Installable CMake package proven by a downstream consumer | `cmake/`, `tests/downstream/` |

## The boundary, in one page

**Consumed**

* Expected path state: a route generation naming its epoch, incarnation,
  revision, source sequence, topology generation and path groups, published by a
  registered authoritative source.
* Topology: devices, ports and links, used only to resolve the link identities an
  observation mentions.
* Observed evidence: hop traces with the generation the source believes it was
  observing, the source incarnation and sequence, the observation and receive
  times, and the proof surface.

**Produced**

* A comparison result: outcome, primary divergence class, the complete class set,
  freshness, completeness, consistency, proof surface, per-evidence judgements,
  hop level differences and an ordered rationale.
* Findings with stable identities, a lifecycle state and an occurrence count.
* Bounded history, and exportable documents for all of the above.

**Never**

* Path Observatory never computes a route, never selects an alternate, never
  writes forwarding state and never claims that a legal path proves delivery.
  Those are other components' authority. If a comparison says `match`, it means
  "the plan and the observation agree on this generation, on fresh, complete,
  consistent evidence" — nothing more.

## The five invariants

These are enforced in code and each has a named test.

1. **Incomplete observation never becomes compliance.** An observation whose hop
   trace is truncated, gapped, missing link identity, or shorter than every plan
   member is `partial`; a partial observation can prove divergence but can never
   produce `match`.
2. **Stale route or topology generations cannot validate current behaviour.**
   Evidence is fenced by epoch, incarnation, route generation and revision before
   anything else happens. A restarted runtime begins with no expected state at
   all and refuses to compare until an authority republishes; restored evidence
   keeps its original receive time and is re-evaluated as stale rather than
   silently becoming fresh.
3. **Legitimate multipath is not false divergence.** A path group may declare
   several members. A trace that agrees with any member is not divergence from
   the plan.
4. **Classification is deterministic.** The engine reads no clock and performs no
   I/O. Its canonical result digest is a function of the *set* of evidence, not
   of arrival order, threading, or how many times a record was delivered. Tests
   permute, reverse and duplicate the evidence and compare digests.
5. **Absence of evidence is never positive evidence.** No evidence, unreadable
   evidence, unregistered sources and unsupported proof surfaces all produce
   `indeterminate` or `unsupported`, never `match`.

## Building

Requirements: CMake 3.25 or newer and a C++20 compiler. MSVC 19.36+, GCC 12+ and
Clang 15+ are the supported toolchains. There are no third party dependencies.

    cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
    cmake --build build/release --parallel
    ctest --test-dir build/release --output-on-failure

On Windows the helper script imports the Visual Studio environment first:

    pwsh -File scripts/build.ps1 -Configuration Release -Test

Options: `PATHOBS_BUILD_TOOLS`, `PATHOBS_BUILD_EXAMPLES`,
`PATHOBS_BUILD_BENCHMARKS`, `PATHOBS_BUILD_TESTS`,
`PATHOBS_WARNINGS_AS_ERRORS` (on by default), `PATHOBS_ENABLE_ASAN`.

The first party warning count is zero under `/W4 /WX` on MSVC.

## Installing and consuming

    cmake --install build/release --prefix /path/to/prefix

    find_package(PathObservatory 1.0 REQUIRED CONFIG)
    target_link_libraries(your_target PRIVATE PathObservatory::core)

`tests/downstream/` is a complete independent consumer. The `pathobs.downstream`
CTest test configures, builds and runs it against an installed prefix, so the
package is proven from outside the build tree on every test run.

## Tooling

    pathobs version
    pathobs selftest
    pathobs compare  [--seed N --diverge --partial --multipath N --pretty --now TIME]
    pathobs explain  [same options as compare]
    pathobs ingest   --store DIR [--seed N ...]
    pathobs history  --store DIR [--limit N --pretty]
    pathobs export   --store DIR [--out FILE --pretty]
    pathobs validate --store DIR
    pathobs serve    --port N [--store DIR --count N --pretty]

`pathobs_node` is the independent observation process used by the transport
tests. It builds a laboratory fabric from its arguments and streams that
fabric's evidence over a real loopback TCP connection:

    pathobs_node --port N [--scenario match|diverge|partial|empty|garbage]
                 [--seed N --hops N --multipath N --evidence-count N]

## Provenance and proof surfaces

Every source states its proof surface, and every comparison result carries the
worst surface among the evidence it accepted.

* **REAL** — evidence produced by real, independently existing infrastructure.
  Path Observatory contains the code path for it (a source may register as
  `real`), but **this repository ships no REAL evidence and proves no REAL
  behaviour**. No switch, ASIC, RDMA, InfiniBand, NVLink or multi host system was
  used to test it.
* **SYNTHETIC** — everything the repository itself produces: the laboratory
  generator in `src/synthetic.cpp`, the examples, the benchmarks, the CLI
  fixtures, the independent process in `pathobs_node`, and every test input.
  This is what the test suite proves behaviour against.
* **UNSUPPORTED** — a surface the build declares it cannot judge. Evidence on
  this surface produces an `unsupported` outcome and never compliance.

## Testing

The suite is 73 tests in six translation units, run by the project's own
framework. **There are no timeouts anywhere**, by design: a test that waits uses
a condition the runtime is required to satisfy, so a liveness defect shows up as
a hang rather than as an intermittent pass.

Results on this machine, MSVC 19.44.35209 (x64), all three configurations green:

| Configuration | Suite | Install | Downstream |
| --- | --- | --- | --- |
| Release (`/W4 /WX`) | 73/73 | ok | ok |
| Debug (`/W4 /WX`) | 73/73 | ok | ok |
| RelWithDebInfo + AddressSanitizer | 73/73 | ok | ok |

AddressSanitizer notes: MSVC ships the ASan runtime with the *Visual Studio
Build Tools* installation, not with the Community installation used for the
Release and Debug configurations, so the ASan configuration is built with the
Build Tools toolset (the same compiler version, 14.44.35207). A longer story:
the ASan configuration was first attempted against the Community installation and
failed at link time with `LNK1104: cannot open file
'clang_rt.asan_dynamic_runtime_thunk-x86_64.lib'` because that installation does
not carry the runtime; the ASan run reported below is a real instrumented run,
not a substituted one. The downstream consumer is instrumented too, because an
ASan instrumented static library has annotation records that a non instrumented
consumer will not link against.

* unit — identities, digests, checked arithmetic, time, canonical encoding, JSON,
  documents, topology, freshness, divergence vocabulary
* integration — the comparison engine against every classification path
* property and seeded randomized — permutation, reversal and duplication
  invariance; 24 seeded scenarios; the compliance implication chain
* adversarial — duplicate JSON keys, 200000 deep nesting, invalid UTF-8,
  unpaired surrogates, oversized frames, wrong magic, bad checksums, tampered
  payloads, destroyed headers, mutated evidence
* concurrency and cancellation — bounded queues, real cancellation, shutdown,
  racing ingest and compare, concurrent readers
* restart and recovery — corruption, truncation, compaction, unclean shutdown,
  and stale evidence staying stale across a restart
* independent process transport — the real `pathobs_node` executable streaming
  to a server hosted by the test, plus a misbehaving process sending garbage

## Benchmarks

    build/release/bench/pathobs_bench_compare 20000
    build/release/bench/pathobs_bench_ingest 200

Both measure completed work and print the completed count next to the elapsed
time, so a truncated run cannot be mistaken for a fast one. Both exit non-zero
if the completed count does not match the requested count. Reported numbers are
from this machine, on synthetic data, and are not a claim about any other
workload.

## Persistence

The store is a versioned, integrity checked append log:

* two alternating header slots, each checksummed and generation stamped, so a
  torn header write still leaves a store that opens;
* per record header and payload checksums, length prefixed and bounded;
* conservative recovery: a corrupt tail is truncated at the record boundary and
  reported, a store whose headers are both invalid is refused outright;
* compaction rewrites the newest records into a temporary file that atomically
  replaces the original;
* loading is not reviving: evidence keeps its receive time, expected state is
  never restored from disk.

## Architecture

```
sources ──► SourceRegistry, SourceSequenceLedger
                    │
expected state ──► TopologyIndex, RouteGeneration ──┐
                    │                               │
observed evidence ──► ObservatoryRuntime ───────────┴──► ComparisonEngine
                       (bounded, ordered)                 (pure, deterministic)
                              │                                  │
                              ├──► PersistentStore               ├──► ComparisonResult
                              ├──► HistoryStore                  ├──► FindingRegistry
                              └──► WorkerPool                    └──► rationale
```

Layering, from the bottom: foundation (identities, digests, checked arithmetic,
time, canonical encoding, JSON) → domain (sources, topology, expected, observed,
freshness, divergence) → engine (comparison, findings, history) → infrastructure
(persistence, transport, workers) → runtime → tooling.

The full design is described in `docs/architecture.md`, the boundary in
`docs/boundary.md`, and the lock audit in `docs/concurrency.md`.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
