# H12 — `SDBG::UniqueNextEdgeBatch` (first composite the filter needs)

**Goal:** batch the *outgoing* half of the `assemble` filter `NextSimplePathEdge(e)==kNullID`.
`UniqueNextEdge` = `ComputeOutgoings` = **`Forward`** (scattered `select`) + a local outdegree scan. So a
batched version = `ForwardBatch` + the **same** scan — reused, not reimplemented.

## How (low-risk extraction, not reimplementation)

Refactored `ComputeOutgoings(e)` → `ComputeOutgoingsFrom(Forward(e))` (behavior-preserving extraction).
`UniqueNextEdgeBatch` then does `ForwardBatch` over the edges and calls the **identical**
`ComputeOutgoingsFrom<WriteOut|MustEq1>` per edge — so the scan logic can't diverge from scalar.

## Evidence — M3 Max, clang 21, real k21 SdBG (30.7 M edges), 2026-06-02 (2M queries, 5 reps)

| | time | speedup |
|---|---:|---:|
| `UniqueNextEdge` scalar | 302 ms | 1.00× |
| `UniqueNextEdgeBatch` | **140 ms** | **2.15×** |

Correctness: `UniqueNextEdgeBatch == UniqueNextEdge` over all 2M — **PASS**.

## Verdict — **PROVEN** (2.15× on the real composite primitive)

Higher than `ForwardBatch`'s 1.88× because `UniqueNextEdge` does *more* scattered-`select` work (it's
Forward-dominated), so batching helps more. The `ComputeOutgoings` extraction is **behavior-preserving** —
validated by re-running the full 500K pipeline and asserting **identical contigs** vs the baseline
(8481 contigs / 5,702,702 bp / N50 703). `UniqueNextEdgeBatch` isn't called by the build yet, so the
extraction is the only behavioral change, and it's a literal code move.

## Next

`UniquePrevEdgeBatch` (the *incoming* half: `Backward` → `rs_w_.select` with a **per-query symbol** → needs a
multi-`c` batched select), then the batched `NextSimplePathEdge` filter, then restructure the first sweep
(windowed collect→batch→classify→walk) — each `megahit --test -t 1` bit-identical before it stays.
