# H5 — 128-byte interleaved (rank9/poppy) layout vs the current 3-array structure

**Hypothesis (the multi-model consensus's #1 pick):** co-locating the cumulative counts with the bit data in
one 128-byte cache line cuts rank from "2–3 cache misses" to 1 → ~2–3× faster rank/select.

**Prediction:** interleaved rank ≥ 2× the current `RankAndSelect<1,2>::rank` on a >L2 bitvector.

## Method

`bench.cpp` (Google Benchmark, clang 21). Real `RankAndSelect<1,2>` vs an interleaved prototype `IRank`
(one 128 B block = `[abs count | 15 data words = 960 bits]`) built over the **same** 512 Mbit bitvector.
Interleaved `rank` verified == reference `rank` over 1M random positions. 2M random `rank` queries,
unsorted and sorted.

## Evidence — M3 Max, Release `-O3 -mcpu=native`, clang 21, 2026-06-02 (2M ranks, 4 reps)

| layout | unsorted | sorted |
|---|---:|---:|
| current (`l2_occ_` + `l1_occ_` + `packed_array_`) | **36.0 ms** | **18.8 ms** |
| interleaved 128 B (counts+data co-located) | 36.7 ms | 20.4 ms |

## Verdict — **DISPROVEN** (for this case)

Interleaving is **not faster** — marginally *slower* (1.02–1.08×). The hypothesis rested on "rank touches 3
separate arrays = 3 misses," but at this scale that's false: `l1_occ_` is **1 MB** and `l2_occ_` **64 KB** —
both **resident in the 16 MB L2** — so a rank is already ~**1 DRAM miss** (the data word) plus two
cache-resident lookups. There were never 3 DRAM misses to coalesce. The interleaved array is also slightly
*larger* (72 MB vs 64 MB) → marginally worse. A fancier rank9 (sub-block counts to shorten the scan) wouldn't
help either: both designs are bound by the single DRAM miss, not by the in-cache popcount scan.

**Sorting still helps rank ~1.9×** (36 → 18.8 ms) for *both* layouts — re-confirming H3 is the lever, layout is not.

## Caveats / scope

Disproven for **rank, 1-bit vector, 512 Mbit**. Could differ where the index does *not* fit cache: the
4-bit **W array** (`RankAndSelect<4,9>` keeps per-symbol occ arrays → larger), **multi-gigabit** indexes that
push `l1_occ_` past L2, or **select** (a binary search over `rank2itv_` + the word scan — a different access
shape, not prototyped here). Worth re-testing there before fully retiring the idea.

## Consequence

The most-confidently-predicted structural lever does **not** pay at genome scale on this hardware. Combined
with H1/H2 (kernel) and H3 (sorting): **a rewrite's value is the query access pattern (batch + sort), not the
bit layout and not the bit-twiddling.** Don't spend the rewrite budget on an interleaved layout on this
evidence.
