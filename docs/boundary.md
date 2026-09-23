# Boundary

Path Observatory observes. It does not control.

## What it consumes

* **Expected path state** from a routing or planning authority: a route
  generation with its epoch, incarnation, revision, source sequence, topology
  generation, path groups and expected hop traces.
* **Topology** from a topology authority: devices, ports and links. Used only to
  resolve the link identities that an observation mentions.
* **Observed evidence** from observation sources: hop traces with the generation
  the source believes it observed, its incarnation and sequence, observation and
  receive times, and its proof surface.

## What it produces

* Comparison results: outcome, primary class, full class set, freshness,
  completeness, consistency, proof surface, per-evidence judgements, hop level
  differences, ordered rationale.
* Findings with content derived identities and an explicit lifecycle.
* Bounded history and exportable documents.

## What it never does

| Not this component's authority | Why it matters |
| --- | --- |
| Computing routes | The plan is an input. Recomputing it would make the observatory a second, disagreeing authority. |
| Choosing alternates | Which member of a group is used is forwarding policy. The observatory reports that one of them was used. |
| Mutating forwarding | The observatory is read only with respect to the fabric. It has no forwarding API at all. |
| Blackhole and loop containment | Detecting a loop is not containing it. The observatory reports divergence; something else decides what to do. |
| Treating path legality as delivery proof | A path that matches the plan is a path the plan describes. It is not proof that any packet traversed it. |

## The compliance statement

`MatchOutcome::Match` is the only outcome that may be described as compliance,
and it is produced only when *all* of the following hold:

* the evidence names the current route generation, epoch and incarnation;
* its revision is the authority's current revision;
* the proof surface can be judged;
* the accepted records agree with each other;
* every accepted record is fresh;
* the observation is complete by the runtime's own derivation;
* at least one accepted record carries advisory authority or better;
* a topology generation is available for link correlation;
* the trace agrees with at least one member of the plan's path group.

Anything else is `divergent`, `indeterminate`, `unsupported` or
`no_expected_state`.

## Honest limits

* The runtime has never been run against a real switch, ASIC, RDMA transport,
  InfiniBand fabric, NVLink domain or multi host deployment. It contains no code
  that models one.
* The independent process transport is a real TCP connection between real
  operating system processes, but both ends are this repository.
* Determinism is proven over the engine's inputs. It does not extend to the
  scheduling of the worker pool, which changes when work happens, not what the
  work concludes.
