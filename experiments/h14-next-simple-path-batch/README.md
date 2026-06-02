# H14 — `SDBG::NextSimplePathEdgeBatch` (the composite the `assemble` first sweep filters on)

## Hypothesis

The `assemble` unitig-building first sweep (`unitig_graph.cpp:22`) tests `NextSimplePathEdge(e) == kNullID`
**once per edge over the whole graph** — a windowable, embarrassingly-batchable filter. Batching it (collect a
window of edge ids → one batched call → classify) should beat the per-edge scalar calls, because the scattered
`select`s underneath get sorted + prefetched (H3/H4/H10).

**Prediction:** a batched `NextSimplePathEdgeBatch` is faster than the scalar loop and `== scalar` exactly. It
will land **between** the outgoing half (H12, 2.15×) and the incoming half (H13, 1.24×), since
`NextSimplePathEdge` = `UniqueNextEdge` (H12) **+ a dependent** `UniquePrevEdge` on the successor (H13).

## Why it needs two phases (the dependency)

`NextSimplePathEdge(e)`: `next = UniqueNextEdge(e)`; keep `next` **iff** `UniquePrevEdge(next) != kNullID`.
Stage 2 depends on stage 1's *result*, so it can't be one flat batch. `NextSimplePathEdgeBatch` does:
1. `UniqueNextEdgeBatch` over **all** edges → `next[]`,
2. gather the survivors (`next[i] != kNullID`) and their indices,
3. `UniquePrevEdgeBatch` over **only** those successors,
4. recombine: `out[i] = next[i]` iff its successor's prev is unique, else `kNullID`.

Composed entirely from the already-`== scalar`-validated H12 + H13 batches, so it equals scalar **by
construction** — no new scan logic.

## Method

`bench.cpp` loads the real k21 SdBG (30.7 M edges), draws **2 M** random edge ids, computes the scalar
reference and the batch, asserts equality, then times both. Build: [`../build-sdbg.sh`](../build-sdbg.sh)
(clang 21, `-std=c++17 -O3 -mcpu=native`). Raw output: [`bench-output.txt`](bench-output.txt).

## Evidence — M3 Max, clang 21, real k21 SdBG, 2 M queries, 5 reps (two independent runs)

| | time (mean) | time (median) | speedup (median) |
|---|---:|---:|---:|
| `NextSimplePathEdge` scalar | 453–468 ms | 451–468 ms | 1.00× |
| `NextSimplePathEdgeBatch` | **365–368 ms** | **357–362 ms** | **~1.25× (1.24–1.31× across reps)** |

Correctness: `NextSimplePathEdgeBatch == NextSimplePathEdge` over all 2 M — **PASS**.
Batch run-to-run spread is wider (cv ≈ 7.6% vs scalar 3.4%) — the gather/scatter + two temp `vector`s and the
data-dependent survivor count add variance.

## Verdict — **PROVEN** (~1.25×, `== scalar`), exactly as predicted

Lands between H12 (2.15×) and H13 (1.24×) and close to H13, because the composite is dominated by its
**incoming** stage: every edge pays the full `UniqueNextEdge` (the 2.15× part is only the *first* of two
stages), then the survivors pay `UniquePrevEdge` (the 1.24× part), plus gather/scatter. The takeaway stands:
**the win is real but modest, and the outgoing half is where batching pays** — so the first-sweep restructure
should batch the `NextSimplePathEdge==kNullID` filter (this primitive) but not expect more than ~1.25× on that
filter alone. The walk that follows (`PrevSimplePathEdge` chains, rc extension) is a sequential dependent
chain and is **not** batchable.

## Next

Restructure the `unitig_graph.cpp` first sweep to a **windowed collect → `NextSimplePathEdgeBatch` →
classify → walk** loop, gated **bit-identical** by [`../verify-contigs.sh`](../verify-contigs.sh)
(baseline `md5 bf2c562…`; current gate result: [`../verify-contigs.last.txt`](../verify-contigs.last.txt)).
Then re-profile `assemble` end-to-end for the real delta — the microbenchmark's ~1.25× is the *ceiling* for
the filter portion of the sweep, not the whole sweep (which also does locking, the unbatchable walk, and rc
extension).
