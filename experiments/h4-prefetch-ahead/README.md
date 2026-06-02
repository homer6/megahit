# H4 — software prefetch-ahead hides the per-query miss (no sort needed)

**Hypothesis:** prefetching the data line for query *i+P* while serving query *i* hides the DRAM miss for
**random** (unsorted) rank, approaching the sorted ceiling — useful when queries arrive in-flight and can't be sorted.

**Method:** `bench.cpp` (Google Benchmark, clang 21). Real `RankAndSelect<1,2>`, 512 Mbit, 2M random rank
queries. `rank(pos)` touches `packed_array_[pos/64]`; we `__builtin_prefetch` that line for query *i+P*.
Sweep P; compare to unsorted (no prefetch) and sorted (the H3 ceiling).

## Evidence — M3 Max, clang 21, 2026-06-02 (2M ranks, 4 reps)

| variant | time | vs unsorted | % of sorted gain captured |
|---|---:|---:|---:|
| unsorted (no prefetch) | 34.6 ms | 1.00× | — |
| unsorted + prefetch P=8 | 24.2 ms | 1.43× | 68% |
| **unsorted + prefetch P=16** | **22.9 ms** | **1.51×** | **76%** |
| unsorted + prefetch P=32 | 23.2 ms | 1.49× | 75% |
| sorted (ceiling) | 19.3 ms | 1.79× | 100% |

## Verdict — **PROVEN**

Prefetch-ahead (P≈16) recovers **~76% of the sorted-batch win without sorting** — pure latency-hiding
(memory-level parallelism). Sorting still wins outright (it adds cache *reuse*, not just latency hiding), and
the two compose. P=16 is the sweet spot (8→16 helps, 32 doesn't).

## Consequence

Confirms the batching/MLP thesis via a second, independent mechanism. The rewrite's batched rank/select path
(issue #4) should **prefetch P≈16 ahead**, and **sort the batch when it's available** — together they target
the ~1.8× ceiling. This is a real lever that needs *no* structural change to `RankAndSelect`.
