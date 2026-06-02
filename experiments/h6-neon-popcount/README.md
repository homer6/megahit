# H6 — explicit NEON multi-word popcount vs scalar (bulk popcount in the build path)

**Hypothesis (a multi-model "agreed win"):** hand-written NEON `vcntq_u8` + widening reduction over multiple
words, with several accumulators, beats scalar `__builtin_popcountll` per word for **bulk** popcount
(`from_packed_array` / `CountCharInWords`), by staying in vector lanes and avoiding per-word GPR↔SIMD `fmov`.

**Method:** `bench.cpp` (Google Benchmark, clang 21). Popcount a **64 MB** (DRAM-resident) array; NEON
(4 accumulators) vs scalar; both verified to agree.

## Evidence — M3 Max, clang 21, 2026-06-02 (4 reps)

| kernel | time (64 MB) | throughput |
|---|---:|---:|
| scalar `__builtin_popcountll` | **0.99 ms** | **63.0 GiB/s** |
| hand NEON `vcntq_u8` ×4 | 1.19 ms | 52.5 GiB/s |

## Verdict — **DISPROVEN**

NEON is **slower**. Bulk popcount over a DRAM-resident array is **memory-bandwidth-bound**: 63 GiB/s is at the
**single-core bandwidth ceiling** of the M3 Max, so the popcount compute is free either way — and the hand-NEON
reduction chain (`vpaddlq` u8→u16→u32) is *extra* work that clang's scalar autovectorization (which already
lowers `__builtin_popcountll` to NEON `cnt`) doesn't pay. SIMD cannot speed up a bandwidth-bound loop.

## Consequence

The build-phase popcount is **not** a SIMD lever. To go faster you need **aggregate** bandwidth → **multicore**
(parallelize `from_packed_array` across P-cores), which is the macro lever already implicated by the CX1
bug (#2) / the coroutine rewrite (#3). Another consensus "win" that measurement retires for this workload.
