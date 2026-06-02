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

clang-format, Google style, C++11 (see `.clang-format`). Format with:
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
