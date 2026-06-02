# H1 / H2 — select-in-word kernel: branchless & consteval-table vs the ctz loop

**Hypotheses**
- **H1:** a branchless select-in-word beats the current `ctz`+shift loop (`kmrns.h::SelectInWord` arm64 `#else`).
- **H2:** a `consteval` byte-**table** select (shift-free) is ≥ the branchless kernel on Apple Silicon.

**Prediction (falsifiable):** branchless ≥ 2× the loop on a tight benchmark; table ≥ branchless.

## Method

`bench.cpp` on Google Benchmark (`brew` 1.9.5), correctness-gated (all 3 kernels == naive over 6.4M
(word,k) cases via `common.hpp`). 8192 independent queries, ~96 KB working set (L1/L2-resident → compute,
not memory). Two access patterns: **throughput** (independent queries) and **latency** (dependent chain —
each result indexes the next, mimicking SdBG traversal). 6 repetitions.

```
clang++ -std=c++20 -O3 -mcpu=native -I .. -I ../../src -I $(brew --prefix)/include \
        bench.cpp -L $(brew --prefix)/lib -lbenchmark -o bench
./bench --benchmark_min_time=0.2s --benchmark_repetitions=6 --benchmark_report_aggregates_only=true
```

## Evidence — M3 Max, Release `-O3 -mcpu=native`, 2026-06-02 (ns per select, 8192/iter; stddev <4%)

| kernel | throughput (independent) | latency (dependent chain) |
|---|---:|---:|
| `select_loop` (ctz+shift, current) | 6.78 | **14.4** |
| `select_branchless` (binary-search) | **5.11** | 24.7 |
| `select_table` (consteval byte-table) | 5.35 | 24.4 |

## Verdict

- **H1: DISPROVEN as originally stated.** Branchless is only **~1.3×** faster than the loop on *independent*
  queries — **not** the **6.8×** an earlier *hand-rolled* `mach_absolute_time` benchmark reported. That number
  was a measurement artifact (uncontrolled input density → larger `k` → the O(k) loop looked far worse, plus
  hand-rolled timing noise). On the **dependent chain — the actual SdBG traversal pattern — the loop is ~1.7×
  FASTER**, because its critical path for small `k` is shorter than the branchless kernel's fixed 6 steps
  (which also pay NEON↔GPR `fmov` round-trips, see the disassembly in `docs/workload-profile.md`).
- **H2: DISPROVEN.** The consteval byte-table is **tied** with the branchless kernel (5.35 vs 5.11 throughput;
  identical latency) — it does not dodge enough to win. Shift-free ≠ faster here.

## Consequence (decision-relevant)

The in-word kernel is **not a worthwhile standalone lever**, and a rewrite justified *only* by "branchless
select" would be a mistake. The branchless form helps **only if queries are made independent (batched)** and
even then only ~1.3×. This redirects effort to the **memory** levers (H3 sorted batch, H4 prefetch, H5 layout)
— and note that batching is *also* the precondition that makes even this small in-word win realizable.

**Methodology lesson (→ performance-engineering skill):** a hand-rolled microbench claimed 6.8×; a
framework-measured, correctness-gated, input-controlled, repeated benchmark showed ~1.3× (and negative on the
real access pattern). Trust frameworks (Google Benchmark) over hand timing; control inputs; report spread.
