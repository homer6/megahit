# docs — performance-engineering research

Reference material informing the planned modernization of this codebase toward **C++23-only on macOS**: dropping the Python driver (`src/megahit`), replacing OpenMP with **Boost.Cobalt** coroutines (the released, installable library we build against today — vendored as `modules/cobalt`; Boost.Capy is the longer-term coroutine-I/O foundation, covered below as research), and weighing SIMD / MLX acceleration for the hot paths. **Reference, not spec** — ground-truth APIs/versions against source before relying on them.

| File | What it is | Relevance to MEGAHIT |
|---|---|---|
| [`boost-capy-and-physical-design.md`](boost-capy-and-physical-design.md) | **Boost.Capy** — C++20/23 coroutine-only I/O foundation (`task<T>`, `when_all`/`when_any`, buffers/streams, the IoAwaitable protocol, HALO frame elision, frame allocators) — plus the **Lakos physical-design principle** (acyclic deps · levelization · low CCD · narrow waist). | The coroutine *direction* for replacing the OpenMP (`#pragma omp` / `omp.h`) parallelism in `src/sorting` (CX1 engine), `src/assembly`, and `src/localasm`, and for expressing the multi-*k* pipeline currently orchestrated by the Python driver. **Capy is the longer-term foundation; the immediate target is Boost.Cobalt** (see the build-notes row below). Also: physical-design guidance for re-levelizing the `src/` module graph. |
| [`macos-simd-and-mlx.md`](macos-simd-and-mlx.md) | Overview of Apple's low-level **`simd`/Accelerate** vector types vs **MLX** (array/ML framework over CPU/GPU/NPU), and where each fits. | Vectorization options for the tight inner loops — k-mer counting/sorting and the SdBG rank/select in `src/kmlib` (`kmrns.h`, `kmbit.h`), which today use portable `__builtin_*` and x86 `_pdep`/popcnt behind `USE_BMI2`. On Apple silicon these map to NEON; `simd`/Accelerate is the CPU-side lever, MLX/Metal the GPU/NPU one. |
| [`mlx-for-bm25f-acceleration.md`](mlx-for-bm25f-acceleration.md) | "Does MLX/Metal accelerate BM25F?" Verdict: **no on the sub-ms hot path** (GPU/NPU dispatch overhead ≫ tiny memory-latency-bound work — stay on CPU); MLX/Metal is a batched/offline throughput lever only. | Cautionary precedent for the assembler: most assembly hot loops are memory-bound graph traversal, so the same "don't offload the latency-bound path" logic likely applies — quantify before reaching for Metal. |
| [`mlx-finetuning-gemma4-lora.md`](mlx-finetuning-gemma4-lora.md) | Apple MLX training-ecosystem scan + a C++23-style MLX LoRA fine-tune example (Gemma). | Tangential to assembly itself; kept here as part of the same Apple-silicon acceleration corpus and as a worked C++23 + MLX integration reference. |

## Build notes

Concrete engineering findings/decisions for this fork (not external research):

| File | What it is |
|---|---|
| [`seastar-guide.md`](seastar-guide.md) | **CURRENT direction** — converting MEGAHIT to a [Seastar](https://github.com/scylladb/seastar) app (shard-per-core, shared-nothing, **C++20-coroutine API**). Navigational map (reactor, coroutines, `sharded<T>`, lifetime/gate, pitfalls, build) tailored to the conversion, with an honest **§0 platform reality** (Seastar is **Linux-only** — collides with the macOS fork) + **architectural fit** (shared-nothing vs MEGAHIT's shared graph → the SdBG-sharding question) and a prototype-first plan. |
| [`coroutine-parallelism-architecture.md`](coroutine-parallelism-architecture.md) | **Prior design (Cobalt + thread-pool — superseded by Seastar, but findings stand).** Code-level inventory of all 64 parallel regions; **root-causes the CX1 arm64 `-t>1` bug** + a fix-by-construction; per-region substrate/overlap table; ranked wins (restore multicore ≫ kill fork/exec ≫ overlap); the fused-MLX verdict (no GPU win flips). Substrate is now Seastar shards, but the inventory, the bug root-cause, and the measured SIMD/MLX constraints carry over. |
| [`boost-cobalt-macos-build.md`](boost-cobalt-macos-build.md) | How to **actually build** against **Boost.Cobalt** on macOS/Apple Silicon (verified — see `src/driver/cobalt_smoke.cpp`): vendor Cobalt's **7 core `src/*.cpp` + `src/detail/*.cpp`** in-tree, define `BOOST_COBALT_SOURCE=1` + `BOOST_COBALT_USE_BOOST_CONTAINER_PMR=1`, link only `boost_container` (no `boost_system` — header-only in 1.90). **Use AppleClang + system libc++, not Homebrew clang** (ABI must match Homebrew's Boost). `-std=c++2b` = C++23; use `cobalt::generator` (`<generator>` absent). |
| [`workload-profile.md`](workload-profile.md) | **Measured profile** — where the assembler spends time and *why*: `assemble` (~56%) is succinct-dBG **rank/select**-bound (IPC 1.46; arm64 `PDEP` gap in `select-in-word`), `local` (~30%) is compute-bound k-mer hashing (IPC 4.07), `count` (~10%) is scan/copy/sort. Plus the **solution space** (multicore, **batching**, SIMD/NEON broadword, sorted/alt search structures, MLX). Raw runs: [`../profiling-history/`](../profiling-history/README.md). |
| [`apple-silicon-architecture.md`](apple-silicon-architecture.md) | **Apple Silicon reference** — memory hierarchy (128 B lines, 16 KB pages, single-core BW), NEON-only ISA (no PDEP/AVX/SVE2), AMX/SME & MLX scope, and the **instruction-level levers** (measured vs candidate) for future wins. Tags [M]easured / [S]ourced. M5 Max carry-forward. |
| [`rewrite-batched-rankselect.md`](rewrite-batched-rankselect.md) | **Evidence-backed rewrite scope** for `assemble` (issue #4): change the rank/select *access pattern* to **batched + sorted/prefetched** (proven 3× / 1.5×), compose with multicore — *not* the kernel (#5), layout (#7), or NEON (#6), all disproven. Design, API sketch, milestones, validation, risks. |

## Experiments & skills

- [`../experiments/`](../experiments/README.md) — hypothesis-driven optimization experiments (Google
  Benchmark / clang 21). Their measured verdicts feed the **Experimental evidence** section of
  `workload-profile.md` (kernel & layout DISPROVEN; sorted-batch PROVEN 3.07×).
- Skills: `.claude/skills/performance-engineering/` (design + run experiments) and
  `.claude/skills/megahit-profiling/` (profile whole runs → `profiling-history/`).

## Provenance

The four research artifacts were captured from external research. The three `boost-capy-*` / `mlx-*` files are **verbatim copies** of the orama-platform research set (the originals live in a separate repo and were read-only here). `macos-simd-and-mlx.md` was **authored from a captured AI overview**, with a provenance banner and the original inline citations preserved.
