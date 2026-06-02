# H13 — `SDBG::UniquePrevEdgeBatch` (the *incoming* half the filter needs)

**Goal:** batch the *incoming* half of the `assemble` filter `NextSimplePathEdge(e)==kNullID`.
`UniquePrevEdge` = `ComputeIncomings` = **`Backward`** (one scattered `select`, **per-query symbol**) + a
local indegree scan. So a batched version = `BackwardBatch` + the **same** scan — reused, not reimplemented.

## Why this needed a *new* batched select

`Forward` (H11/H12) always selects in `rs_last_` with `c==1`, so `select_batch` (single symbol) sufficed.
`Backward` selects in `rs_w_` with `c = LastCharOf(edge)` — **a different symbol per query**. So this drove
`select_batch_multi(cs, ks, out, n)`: composite key `(cs[i]<<48)|(ks[i]&kMask)`, radix-sorted so queries
group by symbol *then* by position before the scattered `InternalSelect(cs, ks)`.

## How (low-risk extraction, not reimplementation)

Refactored `ComputeIncomings(e)` → `ComputeIncomingsFrom(Backward(e))` (behavior-preserving extraction).
`UniquePrevEdgeBatch` does `BackwardBatch` over the edges then calls the **identical**
`ComputeIncomingsFrom<WriteOut|MustEq1>` per edge — the scan can't diverge from scalar.

## Evidence — M3 Max, clang 21, real k21 SdBG (30.7 M edges), 2026-06-02 (2M queries, 5 reps)

| | time | speedup |
|---|---:|---:|
| `UniquePrevEdge` scalar | 277 ms | 1.00× |
| `UniquePrevEdgeBatch` | **224 ms** | **1.24×** |

Correctness: `UniquePrevEdgeBatch == UniquePrevEdge` over all 2M — **PASS**.

## Verdict — **PROVEN** (1.24×, == scalar), but a *smaller* win than the outgoing half

Much less than `UniqueNextEdgeBatch`'s 2.15×, and the reason is structural, not a bug: `Backward` is **one**
scattered `select`, and it's a *smaller fraction* of `UniquePrevEdge`'s cost than `Forward` is of
`UniqueNextEdge`'s. The incoming indegree scan's own accesses sit next to `first_income` in the W-array — so
they're largely cache-resident and already fast; batching can only attack the one `Backward` miss. The
takeaway for the fused filter: **the outgoing half is where batching pays.** Still a real, composing win, and
the extraction is **behavior-preserving** — validated by re-running the full 500K pipeline and asserting
**identical contigs** vs the baseline (`md5 bf2c562…`). `UniquePrevEdgeBatch` isn't called by the build yet,
so the only behavioral change is the literal `ComputeIncomings` code move.

## Next

The batched `NextSimplePathEdge` filter (`UniqueNextEdgeBatch` + `UniquePrevEdgeBatch`), then restructure the
first sweep in `unitig_graph.cpp` (windowed collect→batch→classify→walk) — each `megahit --test -t 1`
bit-identical before it stays. Expect the end-to-end win to track the **outgoing** half (2.15×-ish on the
select-bound portion), diluted by the scan work and the non-batched remainder of the sweep.
