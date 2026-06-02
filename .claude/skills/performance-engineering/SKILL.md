---
name: performance-engineering
description: Design and run hypothesis-driven performance experiments on macOS / Apple Silicon, then record the evidence. Use when optimizing or profiling C/C++ hot paths — forming an optimization hypothesis, building a microbenchmark/kernel, measuring (IPC, ns/op, cache, hotspots), proving or disproving a lever, or deciding whether a rewrite is justified. Covers the arm64 toolchain (Google Benchmark, hyperfine, /usr/bin/time -l, sample, xctrace, Homebrew clang 21), the experiments/ + profiling-history layout, and the evidence discipline (correctness-gate, controlled inputs, repetitions, quiet machine).
---

# Performance engineering (macOS / Apple Silicon)

Turn "X might be faster" into **measured evidence** that proves or disproves it, before committing to a
rewrite. Pairs with the [`megahit-profiling`](../megahit-profiling/SKILL.md) skill (which profiles whole
runs); this skill runs the **focused experiments** that follow from a profile.

## The loop

1. **Profile first** to find the hot path (don't optimize on a hunch). See `docs/workload-profile.md`.
2. **State a falsifiable hypothesis** + a prediction (what number proves/disproves it).
3. **Build a kernel/harness** under `experiments/<id>-slug/` on the shared, *verified* `experiments/common.hpp`.
4. **Gate on correctness** before timing (a fast wrong kernel is worthless).
5. **Measure** with the right tool, on a quiet machine, with repetitions; report spread.
6. **Record the verdict** (PROVEN / DISPROVEN / PARTIAL) + the number in the experiment's `README.md`, update
   `experiments/README.md`, and graduate findings to `docs/workload-profile.md` + a `profiling-history/` entry.

## Toolchain — pick the tool to the question

| Measuring | Tool | Notes |
|---|---|---|
| A C/C++ function/kernel | **Google Benchmark** (`brew install google-benchmark`) | auto iteration/warmup, `DoNotOptimize`/`ClobberMemory`, `--benchmark_repetitions=N`. Don't hand-roll timing. |
| A whole binary / CLI A-B | **hyperfine** (`brew install hyperfine`) | warmups, stats, `--export-json`. For full `megahit`/`megahit_core` stage comparisons. |
| **IPC** + peak memory of a stage | `/usr/bin/time -l` | reports `instructions retired` + `cycles elapsed` (→ IPC) and max RSS. Neither GB nor hyperfine gives this. |
| Function hotspots | `sample <pid> <sec> -f out` | filter idle `__psynch_cvwait` (libomp thread-pool) from the top-of-stack. |
| Cache miss rates | Xcode `xctrace record --template 'CPU Counters'` | Xcode required. |
| Codegen inspection | `lldb -b -o "disassemble -n <fn>"` on a `noinline` extract | see what the compiler actually emits. |

**Compiler:** build experiments with **Homebrew clang 21** (`experiments/build.sh`) — AppleClang 15 is LLVM 16,
too old. Always **record the exact flags** (`-O3`, `-mcpu`, `-std`); they dominate and don't transfer from x86.

## Apple Silicon facts that change the answer (don't carry x86 assumptions)

- **128-byte cache lines** (not 64) — size interleaved/rank9 blocks to 128 B. **16 KB pages** (not 4 KB) — TLB
  pressure already lower; 2 MB huge pages aren't readily available.
- **NEON-only**: no `PDEP`/`PEXT`, no SVE2 `BDEP`. The select-in-word PDEP path is x86-only; arm64 uses
  broadword/table (and `__builtin_popcountll` compiles to NEON `cnt` + GPR↔SIMD `fmov` round-trips).
- **MLX/Metal/AMX** are for dense batched FP, not latency-bound integer rank/select — keep those off the hot path.

## Evidence discipline (or the numbers lie)

- **Correctness-gate** every kernel against a naive reference (`common.hpp::verify_*`) before timing.
- **Control inputs.** Distribution matters (e.g. a select-in-word loop is O(k) — denser words inflate its
  cost). State the input model.
- **Match the access pattern.** Benchmark *both* independent (throughput/ILP/MLP) and dependent-chain
  (latency) — they can give opposite verdicts.
- **Quiet machine, repetitions** (`--benchmark_repetitions`), report mean+stddev; never a single sample.
- A **disproven** hypothesis is a result — record it so it isn't relitigated.

> **Case study (why this discipline):** a hand-rolled `mach_absolute_time` loop reported a select kernel
> **6.8× faster**. A correctness-gated, input-controlled, repeated **Google Benchmark** measurement showed
> **~1.3×** — and *slower* on the real dependent-chain pattern. The hand-rolled number was an artifact.
> Conversely, **sorting a batch of `select` queries measured a robust 3.07×** (compiler-independent) — the
> real lever was the memory access pattern, not the bit-twiddling. (`experiments/h1-*`, `h3-*`.)

## Layout of an experiment

```
experiments/
  common.hpp        # shared verified infra: timing, RNG, kernels, correctness, real-structure builder
  build.sh          # ../build.sh  -> clang 21 + Google Benchmark, native-tuned
  README.md         # hypothesis index (ID, claim, lever/issue, status)
  <id>-slug/
    bench.cpp       # Google Benchmark harness on common.hpp; main() runs verify_* then RunSpecifiedBenchmarks
    README.md       # hypothesis · prediction · method · evidence table · VERDICT · consequence
```

New experiment:
```bash
mkdir -p experiments/h<N>-slug && cd experiments/h<N>-slug
# write bench.cpp using #include "common.hpp"; gate on verify_*; add BENCHMARK(...) cases
../build.sh && ./bench --benchmark_min_time=0.3s --benchmark_repetitions=6 --benchmark_report_aggregates_only=true
```

## Recording results

- Per experiment: fill its `README.md` (evidence table + VERDICT + decision-relevant consequence).
- Update the status column in `experiments/README.md`.
- Graduate confirmed findings into `docs/workload-profile.md` (the solution-space doc) and, for whole-run
  numbers, a dated [`profiling-history/`](../../../profiling-history/README.md) entry (use `megahit-profiling`).
- If a lever justifies work, it maps to a GitHub issue on `homer6/megahit` (the `performance`/`arm64` labels).

## Anti-patterns

- Optimizing before profiling · hand-rolled timing · ignoring the dependent-chain pattern · carrying x86
  assumptions (PDEP, 64 B lines, AVX) onto arm64 · reaching for GPU/MLX on a latency-bound integer path ·
  reporting one sample · not stating the build flags.
