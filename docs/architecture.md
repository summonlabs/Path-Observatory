# Architecture

Path Observatory is layered so that the deterministic core has no dependencies on
time, threads, files or sockets. Everything that is hard to test lives at the
edges; everything that decides is a pure function.

## Layers

| Layer | Files | Depends on |
| --- | --- | --- |
| Foundation | `error`, `checked`, `digest`, `canonical`, `strong_id`, `time`, `limits`, `random`, `json` | nothing but the standard library |
| Domain | `source`, `topology`, `expected`, `observed`, `freshness`, `divergence` | foundation |
| Engine | `compare`, `finding`, `history` | domain |
| Infrastructure | `persistence`, `socket`, `transport`, `worker`, `metrics` | foundation, engine |
| Runtime | `runtime`, `codec`, `documents` | all of the above |
| Tooling | `tools/`, `examples/`, `bench/` | runtime |

`synthetic` sits beside the runtime: it is a generator, clearly labelled, used by
tests, benchmarks, examples and tooling.

## Identity

Two kinds of identity exist, and the difference matters.

**Allocated identities** are 64 bit values tagged with a phantom type:
`PathId`, `HopId`, `DeviceId`, `PortId`, `LinkId`, `SourceId`,
`SourceIncarnationId`, `PathGroupId`, `RouteGenerationId`,
`TopologyGenerationId`, `EpochId`, `IncarnationId`, `RevisionId`,
`SourceSequence`, `StoreIncarnationId`. A `PortId` cannot be passed where a
`DeviceId` is expected, and zero is not a valid identity.

**Content derived identities** are 128 bit truncations of a SHA-256 digest over a
canonical encoding: `EvidenceId`, `ComparisonId`, `FindingId`. These are the
ones whose stability matters, because they are how the same defect is recognised
across runs.

Canonical encoding (see `canonical.hpp`) is fixed width little endian with
explicit lengths and a domain tag. It contains no padding, no hash-map iteration
order and no floating point, which is what makes a digest reproducible on any
machine and any compiler.

## Generational fencing

Every piece of expected state and every piece of evidence carries the full tuple:

    epoch, incarnation, route generation, topology generation, revision,
    source sequence, source incarnation

The runtime fences in this order:

1. **Epoch** — an older epoch is superseded; a newer one is unknown.
2. **Incarnation** — a different incarnation means a different boot; its
   sequences say nothing about this one.
3. **Route generation** — evidence about a generation the plan does not contain
   cannot be compared against the plan.
4. **Topology generation** — a mismatch means link identities cannot be
   correlated against the generation the evidence claims.
5. **Revision** — an older revision is a replay; a newer one is from the future.
6. **Source sequence** — per (source, incarnation); at or below the high water
   mark is a replay, a jump leaves a gap.

Fenced evidence is excluded from the classification entirely and reported in the
result's evidence judgements and class set. It cannot be used to validate
current behaviour.

## The comparison engine

`ComparisonEngine::compare` is a pure function of
`(request, context, policy)`. It reads no clock: the evaluation time is a
parameter. It performs no I/O. It mutates nothing outside its result.

Its steps, in order:

1. Cross-check the generation, epoch, incarnation and topology against the
   authority snapshot.
2. Resolve the subject. An explicitly named group or path wins; if the plan does
   not contain it, the answer is `no_expected_state` rather than a silent
   substitution of whatever the evidence mentions.
3. Judge every evidence record: shape, source registration, declared authority,
   fencing, proof surface, freshness.
4. Sort the records by a total order (authority, freshness, declared
   completeness, receive time, sequence, identity) and drop exact duplicates.
   Every later step reads this order and nothing else, so the caller's order
   cannot influence the outcome.
5. Match traces against every member of the group. Agreement with any member is
   agreement with the plan.
6. Derive completeness from the trace itself — never from the source's claim.
7. Derive consistency: conflicting records are those that claim the same path
   identity and disagree about it.
8. Apply the outcome gates in a fixed order: surface, conflict, freshness,
   divergence, completeness, authority, match. Only the last gate produces
   compliance.

The canonical digest of the result covers the classification and the judged
evidence set. Submission counts are deliberately excluded, so delivering the same
record twice produces the same identity.

## Freshness

`assess_evidence` and `assess_generation` are pure functions of
`(record or timestamps, now, policy)`. Age is measured from the receive time,
because that is the only timestamp on the local clock. The source's own
observation time is used to detect clock skew: ahead of the receive time beyond
tolerance is `future`; behind it beyond the tolerated latency degrades the
judgement to `unknown`. Freshness severity is combined with "worst wins", and
`fresh` is the only value that can support compliance.

Per-source overrides (a source may declare its own maximum observation age) are
applied inside the engine from the registry snapshot, so they stay part of the
deterministic input.

## Persistence

    offset 0     header slot A (64 bytes)
    offset 64    header slot B (64 bytes)
    offset 128   records

Each slot carries a magic, format version, endian marker, flags, creation and
open times, restart count, generation and a CRC-32C over its first 56 bytes. The
store writes the *other* slot on every writable open, so a torn write leaves the
previous slot intact. A read only open never writes, and therefore does not
increment the restart count. A store whose two slots are both invalid is refused rather than
guessed at.

Each record has a 32 byte header (payload length, type, sequence, payload CRC,
header CRC) followed by the payload. Scanning stops at the first invalid record;
with write access the file is truncated at that boundary and the truncation is
reported. Records are read back through the store's own handle, which is why the
handle is mutable: a second read/write open of an already open file is refused on
Windows, and a store that cannot read itself back would be useless.

Expected state is never restored from disk. Evidence, comparisons and findings
are, and restored evidence is re-evaluated against the current time.

## Bounds

Every unbounded quantity has a configured limit and a hard ceiling
(`Limits::hard_ceiling`) that configuration may not exceed: hops per path, paths
and groups per generation, members per group, devices and links per topology,
evidence per comparison and per batch, payload bytes, document bytes, JSON depth
and node count, workers, queue depth, connections, frame size, history records
and window, findings, export records, store bytes, records and record size. A
breach is an error with a stable code, never a silent truncation or an unbounded
allocation.

## Transport

Frames are `magic, version, type, flags, length, CRC-32C` followed by the
payload. The transport moves opaque canonical payloads: it never interprets a
record, so the wire format and the on-disk format cannot drift apart. A stream
that loses framing is closed rather than resynchronised.
