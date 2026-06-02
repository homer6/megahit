# H3 — sorting a batch of select queries (cache locality / HW prefetch)

**Hypothesis:** resolving a batch of `select` queries in **sorted** order is markedly faster than in random
order, with **no change to the data structure** — because sorted rankings make the rank/select index + data
accesses monotonic, so the cache and hardware prefetcher engage.

**Prediction (falsifiable):** sorted ≥ 2× faster than unsorted on a >L2 index.

## Method

`bench.cpp` (Google Benchmark) on the **real** `RankAndSelect<1,2>` over **512 Mbit** (~64 MB packed,
≫ the 16 MB P-core L2 → genuine DRAM behavior), 2,000,000 random `select` queries. Both variants iterate the
query array sequentially (equal array-streaming cost); the *only* difference is the ranking **values**
(sorted vs random), i.e. the locality of the index/data accesses. Sort cost is excluded from the timed loop
(it's O(Q log Q), negligible vs Q memory-bound selects). Built with `../build.sh` (**Homebrew clang 21**).

## Evidence — M3 Max, Release `-O3 -mcpu=native`, 2026-06-02 (2M selects, 4 reps)

| toolchain | unsorted | sorted | speedup | ns/select (unsorted → sorted) |
|---|---:|---:|---:|---|
| AppleClang 15 | 227 ms | 74.0 ms | **3.07×** | 113 → 37 |
| **Homebrew clang 21** | 219 ms | 71.3 ms | **3.07×** | 110 → 36 |

## Verdict — **PROVEN**

Sorting the query batch is **~3.07×** on the memory-bound `select`, and the ratio is **identical across
compilers** — confirming it's a pure memory-locality effect (cache reuse + HW prefetch), not codegen.
Newer clang buys ~3% absolute, nothing structural.

## Consequence (this is the rewrite-justifying lever)

This is the highest-confidence win we've measured. The assembler's `assemble` stage issues billions of
rank/select queries; today they hit the index at random positions (IPC 1.46, memory-latency-bound). A
**batched, sorted query path** — accumulate a frontier of graph nodes' rank/select queries, sort by index,
resolve together — turns random misses into streaming access. Unlike the in-word kernel (H1/H2, ~1.3× and
*negative* on the serial pattern), this is a real ~3× and it's free of structural change to `RankAndSelect`.

Next: **H4** (explicit software prefetch-ahead, on top of / instead of sorting) and **H5** (128 B interleaved
layout) compound on this by attacking the per-query miss directly.
