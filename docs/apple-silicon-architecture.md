# Apple Silicon architecture — facts & instruction-level levers for this fork

A reference for optimizing MEGAHIT on Apple Silicon (Apple M-series; measured on **M3 Max**, target up to
**M5 Max**). Consolidates the multi-model research from this project with what we **measured** in
[`../experiments/`](../experiments/README.md). **Do not carry x86 assumptions** — most of them are wrong here.
Tags: **[M]** = measured on this machine; **[S]** = sourced/synthesis (verify before relying).

Companion docs: [`workload-profile.md`](workload-profile.md) (where the time goes + verdicts),
[`macos-simd-and-mlx.md`](macos-simd-and-mlx.md) (SIMD vs MLX overview),
[`rewrite-batched-rankselect.md`](rewrite-batched-rankselect.md) (the rewrite).

## Memory hierarchy (the dominant factor — this workload is latency/bandwidth-bound)

| | Apple Silicon | x86 (for contrast) | source |
|---|---|---|---|
| **Cache line** | **128 bytes** | 64 bytes | [M] `sysctl hw.cachelinesize`=128 |
| **Base page** | **16 KB** | 4 KB | [M] `hw.pagesize`=16384 |
| P-core L1d / L2 | 128 KB / 16 MB (per cluster) | varies | [M] `hw.perflevel0.*` |
| Huge pages | 2 MB superpages **not readily available** on macOS | hugetlbfs/THP | [S] |
| Single-core mem BW | **~63 GiB/s** (saturates a bandwidth-bound loop) | — | [M] h6 popcount |
| Aggregate mem BW | M3 Max ~400 GB/s; **M5 Pro ~307 vs M3 Pro ~154** | — | [S] |

Implications: **size interleaved/rank9 blocks to 128 B**, not 64. 16 KB pages already cut TLB pressure 4× — huge
pages are low-value/hard here. **A single core cannot use aggregate bandwidth** → bandwidth-bound loops
(popcount, scans) need **multicore**; on M5 the higher BW makes batched random-access workloads scale better.

## Vector / SIMD ISA — NEON only

- **NEON (Advanced SIMD), 128-bit**; P-cores issue ~4× 128-bit ops/cycle. This is the *only* general-purpose
  vector ISA on Apple Silicon. **[S]**
- **No AVX/AVX-512** (x86). **No SVE/SVE2 for normal CPU code** — SVE ops exist *only* inside **SME streaming
  mode** (M4+), which is for matrix work, not general vectorization. **[S]**
- **No `PDEP`/`PEXT`** (BMI2 is x86) and **no SVE2 `BDEP`** → there is *no* single-instruction bit-deposit, so
  **select-in-word must use broadword (SWAR) or a byte table**, never PDEP. **[M]** (kmrns.h `#else` path)
- **Popcount:** arm64 has *no scalar* popcount — `__builtin_popcountll` lowers to NEON `CNT` + GPR↔SIMD `fmov`
  round-trips. Bulk popcount via `vcntq_u8` keeps data in lanes, **but bulk popcount is bandwidth-bound**
  (lever = multicore, not SIMD). **[M]** h6
- **`fmov` (GPR↔SIMD) has latency** — avoid churning scalar↔vector in tight loops; batch work in NEON lanes. **[M]** (h1 disasm showed `fmov` churn in the branchless select)
- Useful NEON ops for this workload: `cnt`/`uaddlv` (popcount+reduce), `tbl`/`tbx` (table lookup — e.g. select-in-byte, base maps), `rbit`/`clz` (ctz), wide loads for batched k-mer ops. **[S]**

## Matrix / ML accelerators — not for rank-select

- **AMX / SME** (matrix coprocessor): outer-products / matmul only. Useless for bit-scan / popcount /
  rank-select (irregular integer, branchy). **[S]**
- **MLX / Metal GPU / Neural Engine:** dense **batched FP** tensor work. Rank/select is latency-bound,
  irregular, integer — the antithesis. Dispatch overhead kills single-query GPU paths; only viable for
  *huge* (10⁴–10⁶) independent batches reformulated as dense ops (offline/throughput). Precedent:
  [`mlx-for-bm25f-acceleration.md`](mlx-for-bm25f-acceleration.md). **[S]** Keep them off the hot path.

## Instruction-level levers (what to try — measured vs candidate)

| Lever | Status | Notes |
|---|---|---|
| **Batched + radix-sorted rank/select** | **[M] PROVEN — 2.7–2.9×** | sort (key,index) pairs with an **LSD radix** (cache-friendly), resolve in order. Implemented: `RankAndSelect::select_batch`. `std::sort` (esp. indirect-comparator) gives the win back to its own cost. |
| **Prefetch-ahead** (`__builtin_prefetch` P≈16) | **[M] PROVEN — 1.5×** | rank: target line known (`packed[pos/64]`) → allocation-free `rank_batch_prefetch`. select: target is post-search → prefetch doesn't help. |
| **Multicore** (12 P-cores) | **[M] macro lever** | the only way to use aggregate bandwidth; assemble already has OpenMP (gated by the CX1 MT bug #2). |
| Broadword / byte-table select-in-word | **[M] minor (~1.3×)** | the PDEP substitute; but the kernel is *not* the bottleneck (access pattern is). |
| 128 B interleaved (rank9) layout | **[M] DISPROVEN** | index already fits L2 → rank already ~1 DRAM miss; nothing to coalesce. |
| NEON bulk popcount | **[M] DISPROVEN** | bandwidth-bound; lever is multicore. |
| NEON k-mer ops (2-bit pack/unpack, reverse-complement, `tbl`) | **[S] candidate** | bit-parallel; relevant to the *compute-bound* `local` stage (IPC 4.07) and `count`'s `CopySubstring`. Untested. |
| Branchless (`csel`/`cinc`) over data-dependent branches | **[M] context-dependent** | helps independent-query throughput; *hurts* dependent chains (longer critical path). |
| `-mcpu=native` / newer clang | **[M] ~null on memory-bound** | clang 21 ≈ +3% vs AppleClang 15; native tuning ≈ null on `count`. |

## Toolchain

- **Compiler:** Homebrew **clang 21** (`/opt/homebrew/opt/llvm/bin/clang++`) — AppleClang 15 is LLVM 16, too
  old. `-std=c++26` compiles; **`std::inplace_vector` not yet in clang 21's libc++** → use `std::array` for
  fixed-capacity stack buffers. Target `-mcpu=apple-m3`/`native`. **[M]**
- Build experiments with [`../experiments/build.sh`](../experiments/build.sh); profile with the
  `megahit-profiling` + `performance-engineering` skills.

## Carrying forward to M5 Max

Same ISA family (NEON-only, no PDEP/SVE2, 128 B lines, 16 KB pages) → **all levers above transfer**. The main
M5 delta is **higher memory bandwidth** [S], which disproportionately helps the bandwidth-bound and batched
random-access paths (the radix-batched rank/select, multicore popcount/scan). Re-measure the [M] numbers on
M5 when available (new `profiling-history/` entries) — the *ratios* should hold, absolute throughput should rise.
