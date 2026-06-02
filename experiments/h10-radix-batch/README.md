# H10 — recover the sorted-batch 3× with a faster (radix) sort

**Hypothesis:** the sorted batch under-delivers (h9: 1.43×) only because `std::sort` is ~half the batch time;
an O(n) cache-friendly **LSD radix sort** of the (key,index) pairs recovers most of h3's locality ceiling (3×).

**Method:** `bench.cpp` (Google Benchmark, clang 21, C++26). Real `RankAndSelect<1,2>`, 512 Mbit, 2M select
queries. Same sorted-resolve; only the sort differs. Radix == per-query verified (PASS).

## Evidence — M3 Max, 2026-06-02 (2M selects, 6 reps)

| ordering | batch time | vs per-query (222 ms) |
|---|---:|---:|
| per-query (no batch) | 222 ms | 1.00× |
| `std::sort` of (key,index) pairs | 166 ms | 1.34× |
| **LSD radix sort** | **82 ms** | **2.72×** |

## Verdict — **PROVEN**, and **promoted to the API**

Radix sort recovers nearly all of the 3.07× ceiling (h3, which excluded sort): **2.72×** here, vs 1.34× for
`std::sort`. The sort algorithm *was* the bottleneck. `RankAndSelect::select_batch` / `rank_batch` now use the
radix sort (`radix_sort_pairs`, 11-bit LSD, early-out) — re-measured **2.88×** in the real API on h9, and
`megahit_core` rebuilds clean.

## Consequence

The batched rank/select primitive now delivers **~2.8×** (select, incl. sort) and **1.5×** (rank, prefetch,
no sort) — both correctness-gated, in `src/kmlib/kmrns.h`. This closes the gap between the proven lever
(h3, 3.07×) and a usable API. The remaining work is integration into the `assemble` traversal (issue #4).
