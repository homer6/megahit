# H15 — foundation batch primitives for the simplifier/pruning sweeps

> **Outcome: these primitives were REVERTED.** They are correct and fast *in isolation* (below), but wiring
> them into the simplifiers/pruning regressed `assemble` 1.41× end-to-end — see
> [`../h16-simplifier-batch-integration/`](../h16-simplifier-batch-integration/README.md). This `bench.cpp`
> references the (now-removed) `OutgoingEdgesBatch`/etc. methods, so it's a historical record — it won't
> compile against the current `sdbg.h`. Kept to document why the lever doesn't pay beyond the first sweep.

## Hypothesis

The first sweep (h11–h14) only batched the `kFlagMustEq1` (unique-edge) scan. The rest of the `assemble` stage
— tip/low-depth/weak-link removal and SDBG pruning — funnels through two other scans over the *same* scattered
`Forward`/`Backward`: `OutgoingEdges`/`IncomingEdges` (`kFlagWriteOut`, collect up to 4) and the degree-zero
predicates (`kFlagMustEq0`). Batched wrappers built the same way as `UniqueNextEdgeBatch` (ForwardBatch/
BackwardBatch + the *identical* scalar scan) should be `== scalar` and win ~2× on the scatter.

## Primitives added (`src/sdbg/sdbg.h`)

- `OutgoingEdgesBatch` / `IncomingEdgesBatch` — `kFlagWriteOut`: out-degree + the up-to-4 edge ids per query.
- `EdgeOutdegreeZeroBatch` / `EdgeIndegreeZeroBatch` — `kFlagMustEq0`: the degree-zero predicate per query.

Each = `ForwardBatch`/`BackwardBatch` (the proven sorted+prefetched select engine) + the byte-identical
`ComputeOutgoingsFrom`/`ComputeIncomingsFrom<flag>` scan reused from the scalar path. Invalid edges map to
`-1`/`false` exactly as the scalar guards do.

## Evidence — M3 Max, clang 21, real k21 SdBG (30.7 M edges), 2 M queries, 3 reps

| | scalar | batch | speedup |
|---|---:|---:|---:|
| `OutgoingEdges` (collect ≤4) | 283 ms | **138 ms** | **2.05×** |
| `EdgeOutdegreeZero` (must-eq-0) | 252 ms | **133 ms** | **1.89×** |

Correctness: `OutgoingEdgesBatch`, `IncomingEdgesBatch`, `EdgeOutdegreeZeroBatch`, `EdgeIndegreeZeroBatch`
all **== scalar** over 2 M random edges (degree + edge ids + predicate) — **PASS**.

## Verdict — **PROVEN** (2.05× / 1.89×, all `== scalar`)

These are the engine for the simplifier/pruning integration (h16): `OutgoingEdges*` feeds the batched
`GetNextAdapters`/`GetPrevAdapters` (tip, weak-link, bubble) and the per-vertex In/OutDegree precompute
(low-depth); the `*DegreeZero*` pair feeds the full-graph `sdbg_pruning` degree-zero sweeps. Whether they pay
*end-to-end* depends on how much of each stage is this scatter vs collapsed-graph/in-memory work — measured in
h16 + the profiling record.
