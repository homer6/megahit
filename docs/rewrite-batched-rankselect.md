# Rewrite scope — batched / sorted / prefetched rank-select traversal

The evidence-backed rewrite direction for the `assemble` stage. **Grounded in measurement, not opinion** —
see [`../experiments/`](../experiments/README.md) and `workload-profile.md` → Experimental evidence. This
scopes the work behind issue **#4**; it is a *plan*, not yet implemented.

## Why (one paragraph)

`assemble` is ~56% of runtime and **memory-latency-bound** (IPC 1.46) on succinct-dBG **rank/select**. We
proved — and disproved — the levers:

- **Disproven:** branchless/table select-in-word (#5), 128 B interleaved layout (#7), NEON build-popcount (#6).
  The kernel, the layout, and SIMD are *not* the bottleneck.
- **Proven:** **sorting** a batch of rank/select queries → **3.07×**; **prefetching** P≈16 ahead → **~1.5×**
  (≈76% of the sorted ceiling, no sort). They compose (~1.8× ceiling) and stack on **multicore** (#3).

So: don't touch the bit-twiddling or the layout. **Change the query *access pattern* from one-at-a-time to
batched + sorted/prefetched.**

## Where the batchable queries are (Milestone 1 audit — DONE)

`assemble` builds a `UnitigGraph` (`unitig_graph.cpp`) with a `#pragma omp parallel for` over **every edge**;
the dominant per-edge cost is the filter `sdbg_->NextSimplePathEdge(edge_idx) == kNullID`. The independent
dimension (across edges) is **batchable** — but the per-edge work is a **composite**, which is the catch:

- `NextSimplePathEdge(e)` = `UniqueNextEdge(e)` [`ComputeOutgoings` → **`Forward`** = scattered `select`]
  **then** `UniquePrevEdge(next)` [`ComputeIncomings` → **`Backward`**]. Two scattered ops, the 2nd dependent on
  the 1st's result — plus a short local degree scan each.
- Edges passing the filter then do a **dependent path-walk** (`PrevSimplePathEdge` loop) + **`EdgeReverseComplement`**
  (`GetLabel` + `IndexBinarySearch` — *not* a simple `select`, not `ForwardBatch`-able), all under `try_lock`.

**`SDBG::ForwardBatch` (built, 1.88× on the real graph) batches only the `Forward` sub-step.** A *correct*
batched filter therefore needs batched **composites** — `UniqueNextEdgeBatch` / `UniquePrevEdgeBatch` =
`ForwardBatch`/`BackwardBatch` **+** the local degree scan replicated — not `ForwardBatch` alone.

## Design (revised after the audit)

1. **Batched composite primitives on `SDBG`** (next): `ForwardBatch` ✅ done; add `BackwardBatch`,
   `UniqueNextEdgeBatch`, `UniquePrevEdgeBatch` (each = batched core op + the inline local scan from
   `ComputeOutgoings/Incomings`). **Validate each `== scalar` on the real graph** (h11-style) before use.
2. **Restructure the first sweep** into windows: per window of edges, `UniqueNextEdgeBatch` → (filter) →
   `UniquePrevEdgeBatch` on the survivors → classify path-ends; then do the dependent walk + RC + lock for the
   few survivors. (RC stays per-edge — it's `IndexBinarySearch`, a separate batching problem.)
3. **Compose with multicore** (#2/#3).

> **Correctness bar:** every step validated **bit-identical via `megahit --test -t 1`** (identical contigs).
> Silent mis-assembly is the failure mode, so this is done as deliberate, reviewed steps — *not* a one-shot
> rewrite. ForwardBatch is the proven foundation; the composite primitives + sweep restructure are the careful
> remaining work.

## Design

1. **Batched API on `RankAndSelect`** (`src/kmlib/kmrns.h`): `rank_batch(const idx[], out[], n)` /
   `select_batch(const k[], out[], n)` that resolve `n` independent queries together:
   - if the batch is in hand → **sort by argument**, resolve in order (cache reuse + HW prefetch), scatter back to original order;
   - if streaming → **`__builtin_prefetch` P≈16 ahead** (the per-query path stays for compatibility).
2. **Restructure the unitig-graph build** to accumulate a **frontier** of edge queries and drain it through the
   batched API, instead of calling `ComputeIncomings/Outgoings` inline per edge.
3. **Compose with multicore** (#3): partition the edge sweep across P-cores once the CX1 MT-sort bug (#2) is
   fixed or superseded by the coroutine rewrite. (Build-phase popcount also wants multicore — h6.)

## Validation (how we'll know it worked)

Re-profile `assemble` with the `megahit-profiling` skill before/after; expect **~1.5–1.8× single-thread** from
the access-pattern change (the measured ceiling), then **× cores** from multithreading. Record as a
`profiling-history/` entry; the in-word kernel and layout stay untouched (proven irrelevant).

## Risks / open questions

- **Batch reachability:** if most of the 56% is the *dependent* unitig walk rather than the independent sweep,
  the single-thread ceiling is lower than 1.8× — Milestone 1 settles this. (Multicore still applies.)
- **Correctness:** the batched API must be bit-identical to the scalar path (gate with the `common.hpp`-style
  reference check).
- **W-array vs bitvector:** both `rs_w_` (4-bit) and `rs_last_` (1-bit) need batched paths; H5's caveat
  (W-array index size) should be re-measured here.

## Sequence

`#2 (fix/replace CX1 MT sort)` → Milestone 1 audit → batched `RankAndSelect` API + correctness → convert the
edge sweep, re-profile → `#3 (multicore)`. Levers #5/#6/#7 are **not** on this path (disproven).
