# Coroutine parallelism architecture — replacing OpenMP + the Python driver

> **Status / provenance.** Design for this fork's modernization, grounded in a code-level inventory of all
> parallelism (64 regions across `src/sorting`, `src/assembly`, `src/localasm`, `src/iterate`, the driver)
> and this repo's *measured* experiments. Captured 2026-06-02 on the `feature/cobalt` branch. **Design, not
> yet built** — every phase below carries a falsifiable hypothesis + a bit-identical correctness gate.

## The model

**Coroutines are the async orchestration layer; they do not compute.** A Boost.Cobalt coroutine issues a
*coarse* unit of work to a substrate, `co_await`s its completion, and runs other orchestration meanwhile. The
FLOPs and bytes happen in the substrate underneath:

- **`asio::thread_pool`** — the real multicore engine. Every current `#pragma omp parallel for` becomes a
  parallel dispatch of N chunked tasks joined by `co_await`. A 1:1 replacement for fork-join, and where almost
  all the parallelism win lives.
- **NEON** — narrow hand-targeted kernels *inside* a thread-pool task (not orchestrated by coroutines).
- **MLX** — batched/offline subproblems dispatched as one coarse async unit the spine `co_await`s while doing
  CPU work. Throughput-only. See [fused MLX kernels](#fused-mlx-kernels) for where fusion + unified memory
  change this.

**What coroutines buy over OpenMP that fork-join structurally cannot express:**
1. **Stage pipelining** across the multi-*k* pipeline — overlap `assemble(k_i)`'s tail with `iterate(k_i)` /
   IO-prefetch for `build_graph(k_{i+1})`. OpenMP joins at every parallel region; the Python driver `wait()`s
   at every stage and round-trips through disk.
2. **IO↔compute overlap** — `co_await` a batch read while the previous batch computes, in-process, structured.
3. **CPU↔accelerator overlap** — dispatch an MLX batch and `co_await` it while the thread-pool chews the
   latency-bound tail.
4. **One process, one address space** — kills fork/exec + the `reads.lib → intermediate_contigs/*.fa` disk
   round-trips between stages.

**Granularity rule (hard).** A coroutine is created only for **stages, *k*-iterations, 64K windows, batches,
and accelerator dispatches** — never per inner-loop iteration. Inner loops over 30.7 M edges / 150 K vertices /
millions of reads stay as **raw loops inside thread-pool tasks** (chunked by thread). A coroutine frame per
edge = frame-alloc death. Dividing line: **iterates ≥10⁴ times ⇒ thread-pool task body, not a coroutine.**

**The honest corollary.** Of the inventoried regions, **~28 fine-grained parallel-fors gain nothing from
coroutines beyond the join** — their value is a plain `co_await parallel_for(pool, range, fn)` plus fixing the
CX1 race by construction. The *pipelining/overlap* leverage is concentrated at **~6 sites**: the driver spine,
`build_library`, the two async-reader sites, `contig_output`→writer overlap, and `MapToContigs`↔read-prefetch.
Don't oversell coroutines at the other 58.

## The CX1 arm64 `-t>1` corruption bug — fixed *by construction*

The inventory root-caused the long-standing bug that pins the whole pipeline to `-t 1`:

- **`src/sorting/base_engine.cpp:332-350`** — in `Lv2Sort`, a mutex (line 333) protects only the *assignment*
  of `thread_offset[tid]`, `acc`, `seen`. After the `lock_guard` destructs (~338), lines 344-350 **re-read
  `thread_offset[tid]` / `bucket_sizes_[b]` without re-locking**. Under `schedule(dynamic)` with many short
  buckets, a thread preempted between release and the read gets a **torn read → wrong output offset**.
- **`src/sdbg/sdbg_writer.cpp:60-66`** — `SaveSnapshot` does a **non-atomic RMW on `cur_thread_offset_[file_id]`**;
  two tasks writing different buckets to the same shard race, last writer wins → corrupted file structure.

The bug class is *lock-protected-write / unlocked-read on shared orchestration state*. Structured concurrency
removes the class via **ownership instead of locking**:
1. **Hoist the offset prefix-sum out, single-threaded, in the spine.** Bucket→output-offset is a serial
   prefix-sum (`acc += bucket_sizes_[b]`); compute it once before launching tasks and pass each bucket its
   offset *as an immutable value*. No shared `acc`/`seen`, no lock, no TOCTOU window.
2. **One output shard owned by exactly one task at a time** — `cur_thread_offset_[file_id]` becomes task-local
   until a `co_await`-join hands it back. The concurrent-`SaveSnapshot` race vanishes.
3. **`co_await`-join is an explicit, typed barrier** replacing OpenMP's implicit one — the next stage
   (OffsetFetcher reading `lv1_special_offsets_`) can't start until the join resolves.

This is the strongest single argument for the effort: the rewrite doesn't just *enable* `-t>1`, it deletes the
bug class.

## Per-region migration (summary)

Full table in the design log; the shape:

| Group | Regions | Substrate | Coroutine leverage | Benefit |
|---|---|---|---|---|
| **CX1 sort** (`src/sorting`) | Lv0/Lv1/Lv2 scans + sort, mercy | thread-pool (work-stealing = dynamic), **offsets hoisted to immutable values, shards single-owner** | join only | **high** — restores multicore on `count` + fixes the bug |
| **assemble** (`src/assembly`) | unitig build, loop/refresh/mark sweeps, pruning, simplifiers | thread-pool; keep `NextSimplePathEdgeBatch` prefetch | join only | **high** (56% stage); `contig_output`↔writer = IO overlap |
| **local** (`src/localasm`,`idba`) | per-contig IDBA (dynamic), hash index build/probe | thread-pool (work-stealing) | `MapToContigs`↔read-prefetch (IO) | **high** — 30%, compute-bound IPC 4.07, ~linear scaling |
| **driver** (`src/megahit`→C++) | the stage pipeline | **Cobalt spine (ORCH)** | **STAGE + IO** | **high** — kills fork/exec + disk round-trips |
| **IO** (`src/sequence/io`) | async reader, decompress | ORCH + thread-pool | **IO** | med (cleaner > faster; already double-buffered) |

**Do NOT touch:** the `main_assemble.cpp:183-249` cleaning loop is a strict serial barrier chain (each
`Refresh()` is a full-graph sync; tips→bubbles→disconnect→prune) — coroutines **cannot** overlap it; the spine
just sequences it. And the 150 K-vertex simplifier sweeps must **not** be batched (measured **1.41× regression**,
[h16](../experiments/h16-simplifier-batch-integration/README.md)) — port them as plain thread-pool parallel-fors.

## SIMD: scalpel, not hammer

- **Genuinely helps (narrow, prove-first):** k-mer *packing/hashing* — 2-bit DNA pack, rolling hash,
  `Lv2ExtractSubString` substring extraction, and the `local` k-mer hashing (compute-bound, IPC 4.07,
  contiguous words). The right workload class for NEON; still gate on a Google Benchmark microbench.
- **Disproven by measurement (do not attempt):** bulk popcount (bandwidth-bound, 63 GiB/s ceiling),
  branchless/table select kernels, interleaved bit-vector layout. Therefore **all rank/select in the assemble
  traversal is off-limits to SIMD** — it's latency-bound (IPC 1.46); the only proven lever there is the
  **memory access pattern** (batch + sort 3.07× + prefetch 1.5×), already partly shipped.

## Fused MLX kernels

**Bottom line up front: no verdict flips.** Fusion (`mx.fast.metal_kernel`, *not* `mx.compile` — which fuses
only elementwise chains, not the gather/scatter the k-mer pipeline crosses) genuinely defeats the
*naive-per-op-dispatch* strawman, and Apple Silicon **unified memory** genuinely removes the CPU→GPU copy tax
(the MLX C++ `array(void*,…)` ctor + shared-storage allocator are zero-copy — which *refutes* the copy claim in
[`mlx-for-bm25f-acceleration.md`](mlx-for-bm25f-acceleration.md), with the caveat that it needs a **16 KB
page-aligned** allocation + `ensure_row_contiguous=false`; MEGAHIT's `malloc`'d `SeqPackage`/SdBG buffers copy
on first GPU use until that's fixed). But neither touches the **access-pattern penalty** or the **bandwidth
roofline**, so fusion+unified-memory help **only** work that is *compute-bound AND batched AND regular-dense* —
and every MEGAHIT hot path misses at least one property.

| Candidate | Verdict | Why |
|---|---|---|
| **`assemble` rank/select** (56%, IPC 1.46) | **OFF** — even fused, even the 30.7 M-edge first-sweep batch | Irregular **latency-bound dependent gather** (rank2itv→L2/L1 binary-search→in-word select); fusion keeps *register* intermediates but here the intermediates **are** the random DRAM misses. GPUs serialize uncoalesced gathers + diverge on per-query binary search. The win on this pattern is **already on CPU** (`select_batch` 2.88×, first-sweep 1.05×; **h16 proved over-batching regresses 1.41×**), and the post-filter walk is an unbatchable serial chain. |
| **`count` extract→encode→bucket** (10%) | **OFF** (real work); speculative-weak hash sub-kernel | Input shape is GPU-ideal (one contiguous 2-bit buffer, fixed-stride windows) **but arithmetic intensity is fatally low** — a few int shift/or/cmp per ~2–6 bytes, **zero FP** → bandwidth-bound (the disproven-NEON-popcount family). The *dominant* cost (`Lv2ExtractSubString`, `kmsort`) is a 65536-way **scatter + radix sort** fusion can't touch. (Note: `count`'s bucket key is a 2-bit-prefix extract, **not** xxhash.) |
| **`local` k-mer hashing** (30%, IPC 4.07) | **SPECULATIVE** — the only genuine GPU-*fit* kernel, but heavily gated | The extract+**XXH3** half is dense/compute-bound/regular (right shape). *But*: (a) it's a **slice of a slice** — the hash immediately feeds a latency-bound `phmap` probe that **can't** be fused (ship `hash[]` back, probe on CPU); (b) wrong granularity today (one tiny graph per contig-end) → needs a re-architecture to a global batched hash pass; (c) **already cache-resident** (that's *why* IPC=4.07), so unified memory adds a handoff that wasn't there. |

**The decisive framing:** every CPU baseline in this repo is *single-threaded* (the CX1 bug). The real competitor
for any GPU offload is the planned **12-P-core coroutine thread-pool** — free, no restructure, bit-identical by
construction. A fused kernel must beat *that*, on only the offloadable slice, after paying dispatch + restructure
+ bit-identical revalidation. That bar is high enough that **there is no STRONG GPU verdict in MEGAHIT.**

**Coroutine composition** (architecture site #3): build the MLX graph once per batch, `mx::async_eval` on a
separate `gpu` stream, `co_await` an awaitable resolved by `mx::synchronize(stream)` — and *while the GPU hashes
this batch, the thread-pool runs the latency-bound CPU probe tail of the previous batch.* Coarse only: one
`co_await` per batched dispatch, never per k-mer (the ≥10⁴-iterates rule). JIT cost is paid once at startup
(build all kernels in the Cobalt init phase; ship the prebuilt `mlx.metallib`, `MLX_METAL_JIT=OFF`).

**Falsifiable hypotheses** (prove-the-lever; bit-identical gate first — XXH3 ported to MSL must match the C
reference exactly; atomic accumulation only for *commutative* ops, never max/first-wins; validate vs contig md5
`bf2c562…`):
- **H-mlx-0 (prerequisite):** measure the warm empty-kernel dispatch floor; if not ≪ per-batch work at the batch
  sizes below, **stop** — nothing can win.
- **H-mlx-1 (`count` hash sub-kernel):** fused canonicalize+extract over a 2-bit read block vs CPU scan, sweep
  B=1e3…1e7. *Kill if* it only wins above 1e6 *and* replaces <~0.3% overall (Amdahl) — predicted: loses (bandwidth-bound).
- **H-mlx-2 (`local` hashing — the only possible win):** fused XXH3 over B 8-byte k-mers, decoupled from the probe,
  vs hashing on the **N-P-core pool** (not one core). *Kill if* GPU end-to-end `local` ≤ multicore `local`. **Don't run
  until multicore is restored** — else you measure against the wrong baseline.
- **H-mlx-3 (put rank/select to rest):** fused Metal `select` over the first-sweep batch vs CPU `select_batch` 3.07×.
  *Expected:* GPU ≤ 1× of single-core CPU batch; OFF stands.

**Where fused MLX can earn its place:** `local` bulk k-mer hashing, as a decoupled hash-then-probe restructure,
flag-gated and defaulted off, *only* if it beats the multicore pool. **Where it must stay off:** the `assemble`
rank/select traversal and the `count` scatter/sort tail. Fusion + unified memory sharpen the reasoning; the prior
"keep MLX off the hot path" conclusion stands.

## Biggest wins, ranked

1. **Restore multicore (gated by the CX1 fix). [BIGGEST]** ~86% of runtime (`assemble` 56% + `local` 30%) is
   currently pinned to one core by the bug. Fixing it (above) and porting the parallel-fors to the pool unlocks
   near-linear scaling on `local` (compute-bound) and large sub-linear on `assemble` (latency-bound). Nothing
   else competes.
2. **In-process pipeline — kill Python fork/exec + disk round-trips. [SECOND]** Dozens of process spawns + GB of
   intermediate disk IO across 3–8 *k* values disappear; SdBG/contig buffers stay in RAM. A guaranteed IO win
   independent of overlap, and the prerequisite for #3.
3. **IO↔compute & CPU↔accelerator overlap. [THIRD, smaller]** Second-order: the async reader already hides most
   IO; MLX helps only the few batchable subproblems. Worth doing, won't rival #1/#2.

## Phased migration + falsifiable hypotheses

Each phase ships behind a flag, is bit-identical-verified against current output, and is recorded in
`profiling-history/` at a fixed sample+subsample+build-flags.

- **Phase 0 — scaffolding.** Stand up `asio::thread_pool` + a `co_await parallel_for(pool,range,fn)` helper;
  wrap one *non-buggy* fine region (e.g. `contig_stat.h` histogram). **H0:** matches OpenMP within ±3% at
  `-t 1` and `-t 4`; *kill if >5% regression* (granularity rule wrong).
- **Phase 1 — fix CX1, multicore the sort.** Hoist offsets to immutable values, single-owner shards, port
  `base_engine.cpp:303/323/357`. **H1a (gate):** bit-identical SdBG at `-t 8` vs `-t 1` over `simple_test`, 50×,
  no assert/SIGSEGV. **H1b:** `count` ≥2.5× at `-t 8`; *kill <1.5×* (bandwidth ceiling).
- **Phase 2 — multicore assemble + local.** **H2a:** `local` ≥0.8×N (near-linear). **H2b:** `assemble` ≥0.4×N
  (sub-linear, latency-bound — the honest target). **H2c:** the 150 K sweeps still regress when batched (guardrail).
- **Phase 3 — in-process spine (kill Python).** **H3a:** ≥10% wall reduction from killed process-spawn + disk
  round-trips alone; *kill <3%*. **H3b:** `--continue` resume still works at every checkpoint.
- **Phase 4 — overlap (only if Phase 3 proves the model).** **H4:** ≥5% beyond H3 from stage/IO overlap; *kill
  <2%* (strict data-flow leaves no slack).
- **Phase 5 — targeted SIMD/MLX (last, narrowest).** **H5a (NEON):** k-mer pack/hash beats scalar ≥1.3× in a
  microbench *and* moves the stage ≥3%. **H5b (MLX):** a fused batched kernel (see fused-MLX section) amortizes
  dispatch + never lands on the assemble traversal.

**Throughout:** single sample+subsample, build flags recorded, `/usr/bin/time -l` (IPC/RSS), `xctrace` (cache),
`sample` (hotspots); correctness gate ahead of every perf claim.
