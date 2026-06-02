<!-- This file is the run directory's README.md (renders when the dir is opened). -->
# Profiling run — 2026-06-02T03:47:21 — first-sweep batched `assemble`

First profile after wiring the **batched rank/select first sweep** into `assemble` (`unitig_graph.cpp`
`is_path_end` precompute via `NextSimplePathEdgeBatch`). Also records the **disproven** attempt to batch the
rest of `assemble` (reverted). Same sample + build flags as the [baseline](../2026-06-01T221830-baseline-single-thread/README.md), so directly comparable.

| Field | Value |
|---|---|
| Date/time | 2026-06-02 03:47 local |
| Git | `f425931` (branch `master`) — first-sweep-only (simplifier batching reverted) |
| Machine | Apple M3 Max — `-t 1` (single-thread; CX1 parallel bug gates `-t>1` on arm64) |
| Sample | SRR341725 — catalog: [`../samples/SRR341725.md`](../samples/SRR341725.md) · subsample: first 500 K read pairs (`sub_{1,2}.fq.gz`) |
| Threads | 1 |

> Results are specific to this sample+subsample — only comparable against runs using the same one (e.g. the baseline).

## Build (the binary these numbers came from)

`build/` — **Release**, `CMAKE_BUILD_TYPE=Release`. Exact `CXX_FLAGS`:

```
-Xclang -fopenmp -I/opt/homebrew/opt/libomp/include -DXXH_INLINE_ALL -ftemplate-depth=3000 -Wall
-Wno-unused-function -fprefetch-loop-arrays -funroll-loops -O3 -DNDEBUG -std=gnu++11 -arch arm64
```

- Optimization: `-O3` · Native tuning: **none** (no `-mcpu`/`-march`) · Std: `gnu++11`
- **Identical flags to the baseline**, so the total-time delta below is a fair A/B (modulo single-run noise).

## Full pipeline — run length

```
build/megahit -1 sub_1.fq.gz -2 sub_2.fq.gz -o OUT -t 1
```

- **Total wall: 240.1 s** (baseline: 250.8 s — ~4% faster, consistent with the assemble win below + run noise)
- k-list: 21,29,39,59,79,99
- Output: **8481 contigs, 5,702,702 bp, N50 703 (max 22,877)** — **md5 `bf2c562…`, byte-identical to baseline** (the first-sweep batch is behavior-preserving)
- Per-stage timeline: [`pipeline-stages.log`](pipeline-stages.log)

## Stage micro-profile — `assemble` (the stage that changed)

The first-sweep batch only touches `assemble`. Measured in **isolation** on the real k21 SdBG (30.7 M edges)
with **hyperfine** (quiet machine, 5–6 runs) — the rigorous delta, not a single full-pipeline sample:

| `assemble` (k21) | wall | vs scalar |
|---|---:|---:|
| fully scalar (baseline behavior) | 38.28 s ± 0.76 | 1.00× |
| **first-sweep batched (this build)** | **36.50 s ± 0.10** | **1.05× faster** ✅ (lower-variance too) |

`count`-stage IPC/RSS are **unchanged** from the [baseline](../2026-06-01T221830-baseline-single-thread/README.md)
(IPC ≈ 2.50, peak RSS ≈ 82 MB) — this round touched only `assemble`, not `count`/`read2sdbg`.

## Disproven this round (recorded, reverted)

Batching the **rest** of `assemble` (tip/low-depth/weak-link/bubble + `sdbg_pruning`, the other 8 sites a
survey found) was bit-identical but **1.41× SLOWER** (54.58 s vs 38.28 s) — the entire regression in
`sdbg_pruning::Trim` radix-sorting all 30.7 M edges ~6× for a cheap early-exiting filter. **Reverted.** A/B:
[`assemble-ab-scalar-vs-fullbatch.md`](assemble-ab-scalar-vs-fullbatch.md). Full analysis:
[`../../experiments/h16-simplifier-batch-integration/`](../../experiments/h16-simplifier-batch-integration/README.md).

## Notes / observations / follow-ups

- **The first sweep is the only batchable win in `assemble`** — it sweeps all 30.7 M raw edges once (a large
  single-pass scatter). Every other sweep runs on the 150 K-vertex collapsed graph or repeats over the full
  graph, where radix-sort overhead ≥ the scatter saved.
- **Macro lever remains multicore** (`-t>1`), blocked by the CX1 arm64 sort-corruption bug.
- Build flags untuned (gnu++11, no `-mcpu`); a `-mcpu=native` / clang-21 build is ~null on memory-bound paths
  (experiments H7), so not pursued for the committed binary.

## Artifacts in this directory

- `pipeline-stages.log` — per-stage timeline of the full 240 s run.
- `assemble-ab-scalar-vs-fullbatch.md` — hyperfine A/B showing the simplifier batching regression (1.41×).
