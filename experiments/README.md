# experiments — hypothesis-driven performance engineering

Each subdirectory is **one falsifiable hypothesis** about a rank/select optimization lever for the
MEGAHIT macOS/Apple-Silicon performance fork. Every experiment is a small, runnable kernel that
**collects evidence to prove or disprove its hypothesis**. Findings graduate to
[`../docs/workload-profile.md`](../docs/workload-profile.md) and dated runs land in
[`../profiling-history/`](../profiling-history/README.md). Driven by the **`performance-engineering`**
skill (`.claude/skills/performance-engineering/`).

## Why this exists

We measured the assembler's hot path (see `docs/workload-profile.md`): `assemble` (~56%) is
succinct-dBG **rank/select**-bound (IPC 1.46, memory-latency-bound; arm64 has no `PDEP`); `local`
(~30%) is compute-bound hashing. Before committing to a rewrite, we **prove each lever pays** — on
*this* hardware (Apple M3 Max: 128-byte cache lines, 16 KB pages, NEON-only, no PDEP/SVE2).

## Method (per experiment)

1. State a hypothesis + a falsifiable prediction (what number would prove/disprove it).
2. Build a kernel/harness on the shared, **verified** [`common.hpp`](common.hpp) (identical timing /
   RNG / kernels / correctness / real-`RankAndSelect` builder across all experiments — results can't drift).
3. Collect evidence on a **quiet machine, serially** (timing contention invalidates numbers):
   `/usr/bin/time -l` (IPC = instructions ÷ cycles), `sample`/`xctrace` (hotspots/cache), `ns/op`.
4. Record verdict (PROVEN / DISPROVEN / PARTIAL) + the number in the experiment's `README.md`.

## Shared infra ([`common.hpp`](common.hpp))

`pe::` namespace: `tick()/ns()` (mach timing), `xs()` (xorshift RNG), three verified select-in-word
kernels — `select_loop` (baseline), `select_branchless` (shift-based), `select_table` (**`consteval`**
byte-table, shift-free) — `verify_select_kernels()`, and `build_random_bitvector()` (real `RankAndSelect<1,2>`).

Build (from an experiment dir): `../build.sh` → links Google Benchmark with **clang 21**. Run on a quiet machine with `--benchmark_repetitions=N` for mean/stddev.

## Hypotheses

| ID | Hypothesis | Lever / issue | Status |
|---|---|---|---|
| **H1** | Branchless select-in-word beats the ctz+shift loop. | branches-out · [#5] | **DISPROVEN as stated** — only ~1.3× on independent queries (not 6.8×, a hand-rolled artifact); ~1.7× *slower* on the dependent traversal chain. See [`h1-select-in-word/`](h1-select-in-word/README.md). |
| **H2** | A `consteval` byte-**table** select (shift-free) is ≥ the branchless kernel on Apple Silicon. | consteval / no-shift · [#5] | **DISPROVEN** — tied with branchless; shift-free ≠ faster. See [`h1-select-in-word/`](h1-select-in-word/README.md). |
| **H3** | **Sorting** a batch of select queries cuts time vs unsorted random (cache locality / HW prefetch), with no structural change. | sorted batches · [#4] | **PROVEN — 3.07×** (compiler-independent). The rewrite-justifying lever. See [`h3-sorted-batch/`](h3-sorted-batch/README.md). |
| **H4** | A batched API with **software prefetch** N queries ahead hides memory latency. | prefetch · [#4] | **PROVEN — ~1.5×** (rank), ~76% of the sorted ceiling, *no sort needed* (P≈16). See [`h4-prefetch-ahead/`](h4-prefetch-ahead/README.md). |
| **H5** | A **128-byte interleaved** (rank9/poppy) layout cuts rank misses 2–3→1 → ~2–3×. | layout · [#7] | **DISPROVEN** (rank, 512 Mbit) — interleaved ≈/slightly slower; the index (`l1_occ_` 1 MB) already fits L2, so rank is already ~1 DRAM miss. Caveat: W-array / select / multi-Gbit untested. See [`h5-interleaved-layout/`](h5-interleaved-layout/README.md). |
| **H6** | **NEON** multi-word popcount speeds `CountCharInWords`/build vs scalar per-word. | NEON · [#6] | **DISPROVEN** — bulk popcount is bandwidth-bound (63 GiB/s single-core ceiling); NEON *slower*. Lever = multicore. See [`h6-neon-popcount/`](h6-neon-popcount/README.md). |
| **H7** | Compiler tuning (`-mcpu=native`, newer clang) is **~null** on the memory-bound hot path. | toolchain | **PARTIAL/confirmed** — `-mcpu=native` ≈ null on `count`; **clang 21 vs AppleClang 15 ≈ +3%** absolute on H1/H3, conclusions unchanged. |

`[#n]` = GitHub issue on `homer6/megahit`. Each row links to its `experiments/<id>-slug/README.md`.

| **H8/H9** | The batched rank/select **API** (sorted + prefetch) realizes the levers in `RankAndSelect`. | API · [#4] | **DELIVERED** — `rank_batch_prefetch` **1.52×** (allocation-free); `select_batch` **2.88×** (radix, see H10). Sort *algorithm* is the cost, not allocation; `std::inplace_vector` not in libc++ yet (C++26 compiles). See [`h9-stack-prefetch-batch/`](h9-stack-prefetch-batch/README.md). |
| **H10** | A radix sort recovers h3's 3× for batched select (vs `std::sort`'s overhead). | sort · [#4] | **PROVEN — 2.72×** (vs 1.34× `std::sort`); **promoted into `RankAndSelect::select_batch`**. See [`h10-radix-batch/`](h10-radix-batch/README.md). |

> **Toolchain:** experiments build with **Homebrew clang 21** via [`build.sh`](build.sh) (AppleClang 15 ≈ LLVM 16 is too old; clang 21 confirmed to not change any conclusion, only ~3% absolute). The repo's `megahit_core` still builds with AppleClang for now.

## Honesty rules

- Quiet-machine serial runs only; report run-to-run spread, not a single sample.
- State the hardware (chip, cache-line, page size) — x86 results don't transfer (no PDEP/AVX here).
- A disproven hypothesis is a *result*, not a failure — record it so we don't relitigate.
