# docs — performance-engineering research

Reference material informing the planned modernization of this codebase toward **C++23-only on macOS**: dropping the Python driver (`src/megahit`), replacing OpenMP with **Boost.Capy** coroutines, and weighing SIMD / MLX acceleration for the hot paths. **Reference, not spec** — ground-truth APIs/versions against source before relying on them.

| File | What it is | Relevance to MEGAHIT |
|---|---|---|
| [`boost-capy-and-physical-design.md`](boost-capy-and-physical-design.md) | **Boost.Capy** — C++20/23 coroutine-only I/O foundation (`task<T>`, `when_all`/`when_any`, buffers/streams, the IoAwaitable protocol, HALO frame elision, frame allocators) — plus the **Lakos physical-design principle** (acyclic deps · levelization · low CCD · narrow waist). | The coroutine model intended to replace the OpenMP (`#pragma omp` / `omp.h`) parallelism in `src/sorting` (CX1 engine), `src/assembly`, and `src/localasm`, and to express the multi-*k* pipeline currently orchestrated by the Python driver. Physical-design guidance for re-levelizing the `src/` module graph. |
| [`macos-simd-and-mlx.md`](macos-simd-and-mlx.md) | Overview of Apple's low-level **`simd`/Accelerate** vector types vs **MLX** (array/ML framework over CPU/GPU/NPU), and where each fits. | Vectorization options for the tight inner loops — k-mer counting/sorting and the SdBG rank/select in `src/kmlib` (`kmrns.h`, `kmbit.h`), which today use portable `__builtin_*` and x86 `_pdep`/popcnt behind `USE_BMI2`. On Apple silicon these map to NEON; `simd`/Accelerate is the CPU-side lever, MLX/Metal the GPU/NPU one. |
| [`mlx-for-bm25f-acceleration.md`](mlx-for-bm25f-acceleration.md) | "Does MLX/Metal accelerate BM25F?" Verdict: **no on the sub-ms hot path** (GPU/NPU dispatch overhead ≫ tiny memory-latency-bound work — stay on CPU); MLX/Metal is a batched/offline throughput lever only. | Cautionary precedent for the assembler: most assembly hot loops are memory-bound graph traversal, so the same "don't offload the latency-bound path" logic likely applies — quantify before reaching for Metal. |
| [`mlx-finetuning-gemma4-lora.md`](mlx-finetuning-gemma4-lora.md) | Apple MLX training-ecosystem scan + a C++23-style MLX LoRA fine-tune example (Gemma). | Tangential to assembly itself; kept here as part of the same Apple-silicon acceleration corpus and as a worked C++23 + MLX integration reference. |

## Build notes

Concrete engineering findings/decisions for this fork (not external research):

| File | What it is |
|---|---|
| [`boost-cobalt-macos-build.md`](boost-cobalt-macos-build.md) | How to actually build against **Boost.Cobalt** on macOS/Apple Silicon: Homebrew's Boost 1.90 ships Cobalt headers but **no compiled `libboost_cobalt`**, so we vendor its five `src/*.cpp` at the matching tag and compile in-tree (link `boost_container`). Plus toolchain facts: use `-std=c++2b` (AppleClang rejects `c++23`), `<generator>` is absent in this libc++ (use `cobalt::generator`). |
| [`workload-profile.md`](workload-profile.md) | **Measured profile** — where the assembler spends time and *why*: `assemble` (~56%) is succinct-dBG **rank/select**-bound (IPC 1.46; arm64 `PDEP` gap in `select-in-word`), `local` (~30%) is compute-bound k-mer hashing (IPC 4.07), `count` (~10%) is scan/copy/sort. Plus the **solution space** (multicore, **batching**, SIMD/NEON broadword, sorted/alt search structures, MLX). Raw runs: [`../profiling-history/`](../profiling-history/README.md). |

## Provenance

The four research artifacts were captured from external research. The three `boost-capy-*` / `mlx-*` files are **verbatim copies** of the orama-platform research set (the originals live in a separate repo and were read-only here). `macos-simd-and-mlx.md` was **authored from a captured AI overview**, with a provenance banner and the original inline citations preserved.
