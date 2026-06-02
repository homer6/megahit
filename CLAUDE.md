# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MEGAHIT is an ultra-fast, memory-efficient NGS assembler optimized for metagenomes. It is built around an
iterative, multi-*k* **succinct de Bruijn graph (SdBG)** assembly pipeline. Despite the README mentioning
`git submodule update --init`, all third-party dependencies (`src/parallel_hashmap`, `src/xxhash`,
`src/pprintpp`, `src/idba`, `kseq.h`) are vendored directly — there are no git submodules and the build does
not need them. External runtime deps: zlib (compressed IO) and OpenMP (parallelism).

## Build

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release   # -DCMAKE_INSTALL_PREFIX=... to set install dir
make -j4
```

The build produces **three copies of the same C++ binary**, each compiled for different CPU instruction
sets, plus the driver:
- `megahit_core` — with `-mbmi2 -mpopcnt` (fastest, requires BMI2 + POPCNT)
- `megahit_core_popcnt` — with `-mpopcnt` only
- `megahit_core_no_hw_accel` — portable fallback
- `megahit` (Python driver) and `megahit_toolkit` (symlink to `megahit_core_no_hw_accel`) are copied into the build dir by custom CMake targets.

Useful CMake options (`-D<OPT>=ON`): `STATIC_BUILD`, `SANITIZER` (ASan/LSan/UBSan), `TSAN` (thread sanitizer),
`COVERAGE`. Debug builds use `-DCMAKE_BUILD_TYPE=Debug` (adds `_GLIBCXX_DEBUG`).

**C++23 (this fork).** `CMAKE_CXX_STANDARD 23` — AppleClang emits `-std=gnu++2b` (it rejects literal `c++23`),
which *is* C++23 mode. Getting there required patching the vendored deps for removed/changed features (search
`[megahit C++23 patch]`): `parallel_hashmap` (`std::result_of`→`std::invoke_result`; `<cstdlib>` for
`std::abort`), `idba/hash.h` (dropped removed `std::unary_function`), `sorting/kmer_counter.cpp`
(`std::memory_order::memory_order_*`→namespace-scope constants). Verified bit-identical after the bump.
**Use AppleClang + system libc++, not Homebrew clang** — Homebrew's Boost libs and `megahit_core` are all
built against the system libc++ ABI; mixing toolchains breaks linking (see `docs/boost-cobalt-macos-build.md`).

## Test

```sh
make simple_test          # runs the toy-dataset suite defined in CMakeLists.txt
./megahit --test -t 2     # single toy run from the build dir
```

`simple_test` exercises many code paths (1-pass mode, no-hw-accel, FASTG conversion, empty/no-contig inputs,
k=255, mercy kmers). There is **no unit-test framework** — tests are end-to-end runs over `test_data/`.
`src/kmlib/test_*.cpp` are standalone manual checks, not wired into the build. To reproduce a single scenario,
copy one of the `COMMAND` lines from the `simple_test` target in `CMakeLists.txt`.

## Code style

clang-format, Google style (see `.clang-format`); the language standard is now **C++23** (see Build). Format with:
`find src -iname '*.cpp' -o -iname '*.h' | xargs clang-format -i --style=file`.

## Architecture

### Two-layer design: Python driver + C++ multi-tool

**`src/megahit`** is a Python 3 orchestrator. It does **no assembly itself** — it parses options, sets up the
output directory, picks the right `megahit_core` binary for the host CPU (`cpu_dispatch()` calls
`checkcpu`/`checkpopcnt`), and invokes `megahit_core` subcommands stage by stage. Key mechanisms:
- **Checkpointing**: the `@check_point` decorator (`Checkpoint` class) records completed stages to a file in the
  output dir so `--continue -o <out>` can resume an interrupted run. Adding/reordering `@check_point`-decorated
  functions changes checkpoint numbering and breaks resume compatibility.
- **Pipeline (see `main()`)**: `build_library` → `build_first_graph` (k_min) → `assemble(k_min)` → then for
  each subsequent *k* in the k-list: `local_assemble` → `iterate` → `build_graph` → `assemble` → finally
  `merge_final`. Intermediate per-*k* contigs land in `<out>/intermediate_contigs/`; the result is `final.contigs.fa`.

**`megahit_core`** (`src/main.cpp`) is a single binary dispatching to subcommands by `argv[1]`. Each subcommand
has a `main_*.cpp` entry point:
- `count` / `read2sdbg` (`src/sorting/`) — build the k_min SdBG from reads (2-pass vs 1-pass `--kmin-1pass`)
- `seq2sdbg` (`src/sorting/seq_to_sdbg.cpp`) — build the SdBG for the next *k* from prior contigs + iterative edges
- `assemble` (`src/main_assemble.cpp`) — simplify the SdBG into contigs
- `iterate` (`src/iterate/`) — extract (k+step)-mer edges to seed the next graph
- `local` (`src/localasm/`) — local assembly to bridge gaps between iterations
- `buildlib` — convert input reads into MEGAHIT's binary sequence library
- toolkit: `contig2fastg`, `readstat`, `filterbylen` (`src/tools/`), plus `checkcpu`/`dumpversion`/`kmax`.

### C++ module map

- **`src/sdbg/`** — the succinct de Bruijn graph: the central on-disk/in-memory data structure all stages read/write.
- **`src/sorting/`** — the **CX1 external-memory sorting engine** (`base_engine.{h,cpp}`). This is the performance
  core: a two-level (Lv1/Lv2) radix-sort over 65536 buckets that builds the SdBG from huge read sets within a
  memory budget. `kmer_counter`, `read_to_sdbg`, `seq_to_sdbg` are concrete engines subclassing it.
- **`src/assembly/`** — graph-simplification algorithms run by `assemble`: `unitig_graph`, `tip_remover`,
  `bubble_remover`, `low_depth_remover`, `weak_link_remover`, `contig_output`. `all_algo.h` is the umbrella header.
- **`src/localasm/`** + **`src/idba/`** — local assembly; `idba/` is vendored IDBA code (hash graph / contig graph)
  used during the local-assembly step.
- **`src/sequence/`** — sequence representation and IO: `Kmer` template, `sequence_package`, and `io/`
  (fastx/kseq, paired-end, binary library readers).
- **`src/kmlib/`** — low-level bit-packing / compact-vector / rank-select primitives and `kmsort`.
- **`src/utils/`** — `options_description` (the C++ option parser used by every subcommand), `utils`, `cpu_dispatch`.

### Key constants & constraints

- **`kMaxK = 255`** (`src/sdbg/sdbg_def.h`) — the largest supported k-mer size; reported by `megahit_core kmax`.
  K-mer storage word counts in `definitions.h` derive from it.
- K values must be **odd**, in range 15–`kMaxK`, with adjacent increments **≤ 28** (enforced in the driver).
- DNA is 2-bit encoded (`kBitsPerChar = 2`, alphabet ACGT); the SdBG W-alphabet is 4-bit.

## Profiling

This is a performance fork, so profiling is first-class. **When profiling, benchmarking, or measuring
runtime/IPC/cache/memory, use the `megahit-profiling` skill** (`.claude/skills/megahit-profiling/`) and
record the result as a datetime-stamped directory under **`profiling-history/`** (committed history; raw
data/outputs stay in the gitignored `profiling/`). Each run dir's record is its **`README.md`**; the data
**sample is catalogued in `profiling-history/samples/`** — results are only comparable at the same
sample+subsample *and* build flags.

Key facts the skill encodes (macOS / Apple Silicon):
- No Linux `perf`. `/usr/bin/time -l` gives wall, peak RSS, and `instructions retired` + `cycles elapsed`
  (→ **IPC**); Xcode `xctrace` gives cache counters; `sample` gives hotspots.
- Profile `megahit_core` *stages* directly — `time -l` on the `megahit` Python wrapper only measures Python,
  not the child processes that do the work.
- Always record the **exact build flags** (`-O3` vs debug, `-mcpu`/native tuning, language std) — they
  dominate results. The committed baseline (`profiling-history/2026-06-01T221830-baseline-single-thread/`) is
  Release `-O3`, *untuned* (no `-mcpu`/`-march`), `gnu++11`.
- **Single-threaded only for now**: the parallel CX1 sort path has a deterministic data-corruption bug on
  arm64 (`-t > 1` → `assert edge_writer.h:72` / SIGSEGV). **Root-caused**: a TOCTOU race in
  `sorting/base_engine.cpp` `Lv2Sort` (offset state `acc`/`seen`/`thread_offset[tid]` read outside the lock at
  ~344-350) + a non-atomic RMW in `sdbg/sdbg_writer.cpp` `SaveSnapshot`. Fix-by-construction (hoist offsets to
  immutable values, single-owner shards) designed in `docs/coroutine-parallelism-architecture.md`. Single-thread is clean.

## Performance experiments & the batched rank/select rewrite

Optimization work is **hypothesis-driven** and lives in [`experiments/`](experiments/) — each `h*/` subdir is
one falsifiable hypothesis with a runnable kernel, evidence, and a PROVEN/DISPROVEN verdict (index:
[`experiments/README.md`](experiments/README.md)). Use the **`performance-engineering`** skill to design/run
new ones. The decisive, partly counter-consensus finding: on this hardware the **memory access pattern** is
the only proven lever — *batch + sort + software-prefetch* the scattered rank/select queries. The kernel
(branchless/table select, H1/H2), the bit-vector layout (interleaved/poppy, H5), and NEON bulk popcount (H6)
were all **measured and disproven**. Proven: sorted-batch select **3.07×** (H3), prefetch-ahead **~1.5×** (H4).

These are being wired into the assembler (the SdBG hot path `assemble` walks is rank/select-bound):
- **`src/kmlib/kmrns.h`** (`RankAndSelect`) — batched API: `rank_batch`/`select_batch` (LSD-radix-sort the
  queries so they hit memory in order, then scan), `select_batch_multi` (per-query symbol), and
  allocation-free `*_batch_prefetch` variants (`__builtin_prefetch` ~16 ahead). `select_batch` → **2.88×** (H10).
- **`src/sdbg/sdbg.h`** — graph-navigation batches on top: `ForwardBatch`/`UniqueNextEdgeBatch` (the *outgoing*
  half of `NextSimplePathEdge`, **2.15×**, H11/H12) and `BackwardBatch`/`UniquePrevEdgeBatch` (the *incoming*
  half, 1.24×, H13). Each composite is a **behavior-preserving extraction** (e.g. `ComputeOutgoings` →
  `ComputeOutgoingsFrom(Forward(e))`) so the batched and scalar scans share the *identical* scan and can't diverge.

**Correctness gate — run after ANY `src/sdbg/sdbg.h` change:** [`experiments/verify-contigs.sh`](experiments/verify-contigs.sh)
rebuilds `megahit_core`, runs the 500K pipeline `-t 1`, and asserts `final.contigs.fa` md5 == the committed
baseline. Every extraction must be **bit-identical** (8481 contigs / 5,702,702 bp / N50 703 on SRR341725
×500K); revert anything that isn't. Integration benches that load a real graph build with
[`experiments/build-sdbg.sh`](experiments/build-sdbg.sh) (links the SDBG TUs at `-std=c++17` — parallel_hashmap
needs `std::result_of`); standalone kernels use `experiments/build.sh` (Homebrew clang 21).

## macOS / Apple Silicon build & known issues

This fork targets macOS / Apple Silicon (see `README.md` for the build recipe; the OpenMP/libomp flags are
required because the upstream CMakeLists never links libomp). Notes:
- The C++ `megahit_core` builds and runs; on arm64 the x86 `-mbmi2/-mpopcnt` paths are inert and runtime
  dispatch selects `megahit_core_no_hw_accel`.
- The `src/megahit` Python driver needed a macOS fix (`os.sched_getaffinity` → `_available_cpus()`); it is
  slated for removal in the planned C++23 rewrite.
- **Known bug:** multithreaded CX1 sort corruption on arm64 (above) — **root-caused**; gates parallel runs
  until the coroutine/thread-pool rewrite lands the fix-by-construction (see Modernization below).

## Modernization direction & docs

The fork now builds at **C++23** (see Build). The next arc — designed in
**`docs/coroutine-parallelism-architecture.md`** — replaces **OpenMP + the Python driver** with a single-process
**Boost.Cobalt coroutine** spine driving three parallelism substrates: `asio::thread_pool` (multicore), NEON
(targeted kernels), and MLX (batched/fused GPU work). Coroutines are the *async orchestration* layer (the
overlap/pipelining fork-join can't express, plus killing the driver's fork/exec + disk round-trips) — the
compute lives in the substrates. Cobalt is vendored as the `modules/cobalt` submodule and is **proven
building/running** on Apple Silicon (`src/driver/cobalt_smoke.cpp` + `src/driver/build-cobalt-smoke.sh`; build
details + the toolchain caveat in `docs/boost-cobalt-macos-build.md`). **Biggest win = restoring multicore**
(~86% of runtime is pinned to one core by the CX1 bug, which the rewrite fixes by construction). Research notes
live in [`docs/`](docs/) (start with `docs/README.md`).

**Honest constraints (from measured experiments — see `experiments/`):** the only proven `assemble` lever is the
**batched memory access pattern** (sort+prefetch scattered rank/select; first-sweep shipped at 1.05×, batching
the rest *regressed* and was reverted). SIMD is a scalpel (k-mer packing/hashing; NEON popcount / branchless-select
/ interleaved-layout are *disproven*). MLX is throughput-only and stays **off** the latency-bound rank/select
traversal. Don't relitigate these without new measurement.

## Skills

Project skills in `.claude/skills/`:
- **`megahit-profiling`** — run + record a profiling pass (see Profiling above).
- **`performance-engineering`** — design and run a hypothesis-driven optimization experiment under
  `experiments/`: form a falsifiable hypothesis, build a kernel, measure (IPC / ns-op / cache / hotspots),
  prove or disprove the lever, then record it (see Performance experiments above). Use when optimizing any
  C/C++ hot path or deciding whether a rewrite is justified.
- **`skill-writer`** — author new Agent Skills (frontmatter, structure, validation). Use it when creating
  more project skills. (Newly added skills load on Claude Code restart.)
