MEGAHIT (Apple Silicon fork)
============================

A hard fork of [voutcn/megahit](https://github.com/voutcn/megahit), being reworked to run fast on
**macOS / Apple Silicon** (Apple M-series, e.g. M5 Max).

MEGAHIT is an ultra-fast, memory-efficient de novo NGS assembler for metagenomes, built on an iterative
multi-*k* succinct de Bruijn graph (SdBG). This fork keeps that assembly core and tears out everything around
it that gets in the way of the Apple Silicon target.

Status
------

Early and under active surgery. An end-to-end Apple Silicon speedup is the **goal**, not a delivered result
yet — but the optimization work is **hypothesis-driven and measured**, not speculative. In progress:

- Removing the Python driver in favor of a **C++23-only** build.
- Replacing **OpenMP** with **C++23 coroutines** on the [Boost.Cobalt](https://github.com/boostorg/cobalt) foundation.
- Evaluating **SIMD** (Accelerate / ARM NEON) and **MLX** for the hot paths (k-mer counting/sorting, SdBG rank/select).
- **Batching the SdBG rank/select hot path.** Profiling pins `assemble` as rank/select-bound. The proven lever
  (on this hardware) is the *memory access pattern* — batch + sort + software-prefetch the scattered queries:
  **3.07×** on sorted-batch select, now being wired into the assembler (`select_batch` 2.88×, `UniqueNextEdgeBatch`
  2.15×), each change gated **bit-identical** against a baseline. The kernel, layout, and NEON-popcount levers
  were tried and **disproven by measurement**.

The evidence lives in [`experiments/`](experiments/) (one falsifiable hypothesis per dir) and
[`profiling-history/`](profiling-history/); design notes and research are in [`docs/`](docs/) — start with
[`docs/README.md`](docs/README.md).

> The legacy Python driver (`src/megahit`) uses a Linux-only call (`os.sched_getaffinity`) and currently
> crashes on macOS before assembly. The C++ `megahit_core` binary itself builds and runs. Retiring the driver
> is part of this fork.

Build (macOS / Apple Silicon)
-----------------------------

```sh
brew install cmake libomp        # zlib comes with the system

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DOpenMP_C_FLAGS="-Xclang -fopenmp -I/opt/homebrew/opt/libomp/include"   -DOpenMP_C_LIB_NAMES=omp \
  -DOpenMP_CXX_FLAGS="-Xclang -fopenmp -I/opt/homebrew/opt/libomp/include" -DOpenMP_CXX_LIB_NAMES=omp \
  -DOpenMP_omp_LIBRARY=/opt/homebrew/opt/libomp/lib/libomp.dylib \
  -DCMAKE_EXE_LINKER_FLAGS="-L/opt/homebrew/opt/libomp/lib -lomp"
make -j
```

The extra OpenMP flags are needed because Homebrew's `libomp` is keg-only and AppleClang won't link it on its
own. On arm64 the x86 hardware-acceleration paths are inert, so the runtime CPU dispatch selects the
`megahit_core_no_hw_accel` binary. (This whole dance is exactly why OpenMP is being replaced — see the roadmap.)

Usage
-----

```sh
megahit -1 pe_1.fq.gz -2 pe_2.fq.gz -o out          # one paired-end library
megahit -1 a1.fq,b1.fq -2 a2.fq,b2.fq -r se.fq -o out  # multiple PE libraries + single-end
```

Contigs are written to `out/final.contigs.fa`. Run `megahit -h` for the full option list.

Upstream & credit
-----------------

Forked from **MEGAHIT** by Dinghua Li et al. (The University of Hong Kong & L3 Bioinformatics):
<https://github.com/voutcn/megahit>. For the upstream project's packaged releases (BioConda, Docker,
prebuilt Linux binaries), use the original repository.

- Li, D., Liu, C-M., Luo, R., Sadakane, K., and Lam, T-W. (2015) *MEGAHIT: An ultra-fast single-node solution
  for large and complex metagenomics assembly via succinct de Bruijn graph.* Bioinformatics.
  doi:[10.1093/bioinformatics/btv033](https://doi.org/10.1093/bioinformatics/btv033) — PMID 25609793.

License
-------

GPLv3, inherited from upstream MEGAHIT — see [LICENSE](LICENSE). This is a modified fork.
