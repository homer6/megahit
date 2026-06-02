# MEGAHIT workload profile (macOS / Apple Silicon)

What the assembler actually spends its time on, *why*, and the solution space for speeding it up on
Apple Silicon. Measured, not assumed. Companion to the run records in
[`../profiling-history/`](../profiling-history/README.md) (which hold the raw captures and exact commands).

> **Status / caveats.** All numbers below are **single-threaded** (the parallel CX1 sort path crashes on
> arm64, `-t > 1`), Release `-O3` (`-mcpu=native` variant noted), on **Apple M3 Max**, sample
> **SRR341725** (Qin 2012 T2D gut metagenome) subsampled to **500K read pairs**, 90 bp, GC ~43%
> (see [`../profiling-history/samples/SRR341725.md`](../profiling-history/samples/SRR341725.md)). Profiling
> is sample- and build-dependent — these shapes are representative, the exact percentages are not universal.

## TL;DR

- Time is dominated by **two memory-/throughput-bound, irregular *integer* kernels**, not by floating point
  or dense linear algebra:
  - **`assemble` ~56%** — succinct de Bruijn graph **rank/select** traversal. **IPC 1.46** → stalled
    (pointer-chasing the rank/select index) *and* instruction-heavy. The dominant primitive (`InternalSelect`)
    is **crippled on arm64**: no `PDEP`, so it falls back to an O(n) `Ctz` loop.
  - **`local` ~30%** — IDBA local assembly: **hash-table k-mer probing + k-mer hashing/reverse-complement**.
    **IPC 4.07** → compute-bound, small per-contig graph resident in cache.
  - **`count` ~10%** — read scanning (k-mer rolling) + 2-bit substring extraction/copy + radix sort. IPC ~2.5.
- The single-thread levers split by kernel: `assemble` wants **lower-latency / batched rank-select** (and an
  arm64 `select-in-word` fix); `local` wants **fewer/cheaper instructions** (it's already running near peak IPC).
- The macro lever is **multicore** (12 P-cores; the code already has OpenMP in `assemble`) — gated by the
  CX1 bug / the planned coroutine rewrite.
- **MLX and SIMD are real candidates, but conditionally**: the hot work must first be **batched** (turn
  billions of *independent* single-query pointer-chases into bulk operations) before SIMD/GPU/NPU can apply.
  Batching is the bridge, not an afterthought.

## Experimental evidence (measured verdicts — override the brainstorm below)

Hypothesis-driven experiments (Google Benchmark, Homebrew clang 21, M3 Max — see
[`../experiments/`](../experiments/README.md), driven by the `performance-engineering` skill) have **tested**
the candidate levers. Where these contradict the multi-model "Solution directions" further down, **the
measurement wins.**

| Lever | Verdict | Measured |
|---|---|---|
| in-word select kernel (branchless / `consteval` table) | **DISPROVEN as stated** | only ~1.3× on *independent* queries — a hand-rolled bench's "6.8×" was an artifact — and ~1.7× **slower** on the dependent traversal chain; the table ties the branchless kernel (`h1-select-in-word`) |
| **sorted-batch select** | **PROVEN — 3.07×** | compiler-independent (cache locality + HW prefetch); **the rewrite-justifying lever** (`h3-sorted-batch`) |
| **prefetch-ahead** | **PROVEN — ~1.5×** | `__builtin_prefetch` P≈16 ahead on an unsorted batch captures ~76% of the sorted ceiling — latency-hiding, *no sort needed* (`h4-prefetch-ahead`) |
| **batched API** (in `kmlib::RankAndSelect`) | **DELIVERED** | `rank_batch_prefetch` **1.52×** (zero-allocation); `select_batch` via (key,index) pair-sort **1.43×** incl. sort. The sort *algorithm* is the cost (an indirect-comparator sort regressed) — not allocation; `std::inplace_vector` isn't in libc++ yet though C++26 compiles (`h8`/`h9`) |
| interleaved 128 B (rank9) layout | **DISPROVEN** (rank, 512 Mbit) | ≈/slightly slower — `l1_occ_`(1 MB)+`l2_occ_`(64 KB) already fit L2, so rank is already ~1 DRAM miss; nothing to coalesce (`h5-interleaved-layout`). Caveat: W-array / select / multi-Gbit untested |
| NEON bulk popcount (build) | **DISPROVEN** | bulk popcount is bandwidth-bound (63 GiB/s single-core ceiling); hand-NEON *slower* than scalar — lever is multicore, not SIMD (`h6-neon-popcount`) |
| toolchain (`-mcpu=native`, clang 21) | ~null | clang 21 ≈ +3% vs AppleClang 15; conclusions unchanged (`h7`) |

**Bottom line for the rewrite:** the *proven* levers are all about the **memory access pattern** — **sort the
batch** (3×) and/or **prefetch P≈16 ahead** (1.5×, no sort), which compose toward a ~1.8× ceiling and stack on
**multicore** (#3). Every *compute*-side and *layout*-side idea was **disproven by measurement** — the
branchless/table kernel (#5), the interleaved 128 B layout (#7), and NEON build-popcount (#6) — despite being
the most-confidently-recommended. Spend the rewrite budget on a **batched, sorted, prefetched SdBG traversal**
(issue #4) plus restoring **multithreading** (#2/#3); do *not* spend it on broadword select or an interleaved layout.

## Where the time goes (measured)

Full single-threaded pipeline ≈ **239 s** (`-mcpu=native`) / **251 s** (untuned `-O3`) for 500K pairs.

| Stage | Share | IPC | Peak RSS | Bound by |
|---|---:|---:|---:|---|
| `assemble` (unitig graph build + cleaning) | ~56% | **1.46** | 117 MB | memory latency (rank/select index) + instruction count |
| `local` (IDBA local assembly) | ~30% | **4.07** | 185 MB | compute throughput (in-cache hashing) |
| `count` / SdBG build (sort) | ~10% | ~2.5 | 82 MB | mixed: streaming scan/copy + sort scatter |
| `iterate` + `seq2sdbg` | ~4% | — | — | graph rebuild between k |

Per-k `assemble`: 38/34/29/18/13/9 s (k=21→99); per-k `local`: 10/12/18/20/15 s.

### Hotspots (`sample`, top-of-stack, idle thread-pool waits removed)

**`assemble` (k=21):**
```
RankAndSelect<4,9,…>::InternalSelect   10489   ← W array (edge labels) select
RankAndSelect<1,2,…>::InternalSelect   10148   ← bit-vector (last/flags) select
SDBG::ComputeIncomings<5>               1532
RankAndSelect<4,9,…>::InternalRank      1427
RankAndSelect<1,2,…>::InternalRank      1134
SDBG::ComputeOutgoings<5>                223
SDBG::IndexBinarySearch                  194
UnitigGraph::VertexToDNAString           160
.omp_outlined..*                       (parallel regions present, idle at -t1)
```
**Select ≫ Rank** (~20.6K vs ~2.6K). Graph traversal = constant rank/select on the BWT-like structure;
`ComputeIncomings/Outgoings` call them per node. 213 B instructions at IPC 1.46.

**`local` (k=21):**
```
Sequence::GetIdbaKmer                   1037
phmap flat_hash_map::find<Kmer>          938   ← k-mer → contig map probe
HashMapper::TryMap                       868
IdbaKmer::ReverseComplement              497
HashTableST::find_or_insert              385
HashGraph::Assemble / InsertKmers / FindVertexAdaptor …
```
Hash-table probing + k-mer hashing/RC, on small per-contig structures → high IPC (4.07), cache-resident.

**`count` (k=21):**
```
KmerCounter::Lv1FillOffsets             3320   ← roll k-mers along reads, compute bucket keys
KmerCounter::Lv2ExtractSubString        2300   ┐ 2-bit substring extract/copy
CopySubstringRC                          247   ┘ (+ reverse complement)
kmsort radix lambda (Substr<2,2>)        681   ← the sort itself
KmerCounter::Lv2Postprocess              433
KmerCounter::Lv0CalcBucketSize           390
```
Read-scan (~50%) + substring copy (~35%) + sort (~9%). The sort is a small fraction at this scale.

## Why (kernel characterization)

- **`assemble` is rank/select-bound.** Each graph step does rank/select on the succinct dBG. `Select` (find
  the i-th set symbol) is the costly primitive: a multi-level index walk (L2 → L1 → packed array — a
  cache-miss-prone **pointer chase**, hence IPC 1.46) plus an **in-word select**. On x86 the in-word step is
  one `PDEP`+`CTZ`; **on arm64 there is no `PDEP`**, so `kmrns.h::SelectInWord` takes the `#else` branch — an
  **O(num_c) loop of `CTZ`+clear-bit** (`src/kmlib/kmrns.h:268`). So arm64 pays both: the latency of the index
  walk *and* extra retired instructions in the select loop (the 213 B instruction count). Same `PDEP` gap
  applies to the radix-sort `Pdep` path.
- **`local` is compute-bound.** Local assembly rebuilds a *small* hash graph per contig neighborhood; it fits
  in cache, so probing is fast and IPC is high (4.07, near the M3 P-core's wide-issue ceiling). The cost is
  sheer instruction volume: k-mer extraction (`GetIdbaKmer`), reverse-complement, hashing, and probe/insert.
- **`count` is balanced** between streaming read-scan, 2-bit substring packing/copy, and the radix sort. At
  this input size the sort is minor; scanning + copying 2-bit-packed k-mers dominates.

## Solution directions (candidates to evaluate)

Ranked roughly by expected leverage. These compose; **batching is the enabler that unlocks SIMD/GPU**.

### 1. Multicore parallelism — the macro lever
`assemble` already contains OpenMP regions (`.omp_outlined` in the profile); we measured `-t 1` only because
the parallel CX1 sort corrupts on arm64. Restoring parallelism across the **12 P-cores** is the largest
single speedup for the dominant stages. This is the natural payoff of the **OpenMP → Boost.Cobalt coroutine
rewrite** (single-process orchestration + a thread-pool for CPU-bound stages). *Evidence:* serial IPC of 1.46
(assemble) means cores sit idle on memory; many independent traversals can run concurrently.

### 2. Batching — turn latency-bound pointer-chases into bulk work
A *single* rank/select or hash probe is a latency-bound pointer chase you cannot vectorize. But the assembler
issues **billions of independent ones**. **Batching** independent queries (e.g., collect the rank/select
queries for a frontier of graph nodes, or a block of reads' k-mer probes, and resolve them together) exposes
**memory-level parallelism** (many outstanding loads hide latency), enables **SIMD across the batch**, and is
the only form in which **GPU/NPU offload** could ever pay off. This is the bridge to (3)–(5). *Targets:*
`assemble` rank/select, `local` hash probes, `count` scanning.

### 3. SIMD / NEON / broadword (SWAR) — surgical, arm64-specific
Not Apple's `simd`/Accelerate *library* (that's FP/matrix — we have none of that); rather hand-written
NEON / broadword bit manipulation on the hot primitives:
- **`select-in-word`**: replace the arm64 `Ctz` loop with a **constant-time broadword select** (or NEON) —
  directly attacks `assemble`'s #1 hotspot and closes the x86 `PDEP` gap.
- **k-mer ops**: NEON for 2-bit pack/unpack (`CopySubstring`) and **reverse-complement** (`count`, `local`).
- **branchless / SIMD binary search** for `SDBG::IndexBinarySearch`.
Combine with (2): a batched, SIMD-friendly rank/select that resolves N queries per call.

### 4. Sorted / alternative search structures
Attack the latency directly via data structure + layout:
- **rank/select index layout**: cache-friendlier sampling/interleaving of the L1/L2 occurrence arrays, wider
  superblocks, or a select-optimized index (the dominant op is *select*, which the current structure samples
  for less aggressively than rank).
- **branchless / Eytzinger layouts** for the binary searches.
- **hash table**: verify whether `phmap`/`HashTableST` use SIMD group probing on arm64 (NEON) or fall back to
  scalar; an arm64-tuned open-addressing probe could cut `local`'s instruction count.
- prefetch-friendly node ordering so graph walks touch memory predictably.

### 5. MLX / GPU / NPU — for batched, reformulated, throughput subproblems
MLX shines on **dense, batched, uniform** work, and as a **batched/offline throughput** lever (not sub-ms
single-query latency — see [`mlx-for-bm25f-acceleration.md`](mlx-for-bm25f-acceleration.md)). Candidate shapes
*if* reformulated via (2): bulk k-mer hashing/counting, batched membership/rank queries reframed as array ops,
or offline index construction. The current per-node traversal is the wrong shape for it; a batched reformulation
is the prerequisite. Worth a scoped experiment on the most batchable phase (k-mer counting / `count`) before
committing.

### 6. Data layout & prefetching — attack latency directly
`kmrns.h` already issues `__builtin_prefetch` in the rank/select walk; evaluate software-pipelining the graph
traversal (prefetch the next node's index lines while computing the current), and restructuring SdBG storage
for the access pattern. Pairs naturally with batching (2).

### 7. Reduce work algorithmically
Cache `ComputeIncomings/Outgoings` results where revisited; fewer select calls per unitig step; cheaper
graph-cleaning passes (`assemble` runs 5 cleaning rounds by default).

## Open questions / next measurements

- **Cache-miss rates** for `assemble` via `xctrace` CPU counters — to quantify how much of IPC 1.46 is L1D/L2
  misses (index pointer-chase) vs the select-loop instructions. (IPC is strong indirect evidence; a direct
  miss rate bounds the batching/layout vs select-fix split.)
- **Per-hotspot instruction breakdown** inside `InternalSelect` — what fraction is the `Ctz` loop (bounds the
  broadword-select win) vs the index loads (bounds the layout/prefetch win).
- **Multithread scaling** of `assemble`/`local` once the CX1 bug (or the coroutine rewrite) lands.
- **`phmap` arm64 probing** — NEON group probe vs scalar fallback.
- Re-profile on a **modern 150 bp sample** (larger usable k, shifts the stage balance) and at **full scale**
  (non-subsampled), where the radix sort's share grows.
