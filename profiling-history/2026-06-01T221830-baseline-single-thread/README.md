# Profiling run — 2026-06-01T22:18:30 — single-threaded baseline (M3 Max)

First baseline for the macOS / Apple-Silicon fork. Single-threaded on purpose: the parallel CX1
sort path crashes on arm64 (see **Known issues**), and IPC / cache are per-core metrics anyway, so
a single-core run is a valid *first* baseline. This is the "before" — untuned `-O3`, still `gnu++11`.

| Field | Value |
|---|---|
| Date/time | 2026-06-01 22:18:30 (local) |
| Git | `9574891` (branch `master`) |
| Machine | Apple **M3 Max** (Mac15,9) — 12 performance + 4 efficiency cores, 64 GB |
| OS / compiler | macOS 14.6.1 · AppleClang 15.0.0 (clang-1500.3.9.4) |
| **Sample** | **SRR341725** (human gut metagenome, Qin 2012 T2D) — full characterization: [`../samples/SRR341725.md`](../samples/SRR341725.md) |
| Subsample | first **500,000 read pairs** per mate (`head -n 2000000`; md5 `8664438c…` / `ad3700bf…`), 90 bp reads |
| Threads | **1** |

> Results are specific to this sample+subsample. Comparisons are only valid against runs using the same
> [`SRR341725`](../samples/SRR341725.md) 500K-pair subsample.

## Build (the binary these numbers came from)

`build/` — **Release**, `CMAKE_BUILD_TYPE=Release`. Exact `CXX_FLAGS`:

```
-Xclang -fopenmp -I/opt/homebrew/opt/libomp/include
-DXXH_INLINE_ALL -ftemplate-depth=3000 -Wall -Wno-unused-function
-fprefetch-loop-arrays -funroll-loops
-O3 -DNDEBUG -std=gnu++11 -arch arm64
```

- **Optimization: `-O3 -DNDEBUG`** (not debug).
- **No native tuning:** no `-march` / `-mcpu` / `-mtune` — generic ARMv8 codegen, **not** tuned for M3 Max.
- `-mbmi2`/`-mpopcnt` are x86-only in CMakeLists and correctly absent on arm64; all three core binaries are identical here.
- Language standard still **`gnu++11`** (C++23 migration hasn't touched the build yet).
- `-fprefetch-loop-arrays` is a GCC flag clang largely ignores on arm64.
- Binaries: full run used `megahit_core_no_hw_accel` (runtime CPU dispatch → no-hw-accel on arm64); `count` micro-profile used `megahit_core` (identical codegen on arm64).
- Driver: `src/megahit` with the macOS portability fix (`os.sched_getaffinity` → `_available_cpus()`), required to run on macOS at all.

## Full pipeline — run length

```
build/megahit -1 profiling/data/sub_1.fq.gz -2 profiling/data/sub_2.fq.gz -o profiling/prof_out -t 1
```

- **Total wall: 250.78 s**
- k-list (auto, capped by 90 bp reads): **21, 29, 39, 59, 79, 99**
- Output: **8,481 contigs, 5,702,702 bp, N50 703 bp** (min 204, max 22,877, avg 672)

### Phase breakdown (single-thread, derived from stage timestamps — see `pipeline-stages.log`)

| Phase | ~Time | Share |
|---|---:|---:|
| Library build + first SdBG (count, k=21) | ~18 s | ~7% |
| Contig assembly (unitig-graph simplification), all k | ~141 s | ~56% |
| Local assembly, all k | ~75 s | ~30% |
| Iterate + `seq2sdbg` (graph rebuild between k) | ~25 s | ~10% |
| Merge final | <1 s | — |

Per-k assemble: 38 / 34 / 29 / 18 / 13 / 9 s (k=21→99). Per-k local assembly: 10 / 12 / 18 / 20 / 15 s.

**Observation:** at this input size single-threaded, time is dominated by **graph simplification (`assemble`, ~56%) + local assembly (~30%)**; SdBG construction (sorting) is cheap (~10%). The CX1 sort is where parallelism matters most for *large* inputs, but the *serial* hot path here is unitig cleaning + local assembly.

## Stage micro-profile — `count` (SdBG construction), single-thread

```
/usr/bin/time -l build/megahit_core count -k 21 -m 2 --host_mem 61847529062 --mem_flag 1 \
  --output_prefix <prefix> --num_cpu_threads 1 --read_lib_file <reads.lib>
```

| Metric | Value |
|---|---|
| Wall | **9.41 s** (user 9.25, sys 0.13) |
| Instructions retired | 88,243,767,160 |
| Cycles elapsed | 35,320,274,700 |
| **IPC** | **≈ 2.50** (0.40 cycles/instr) |
| Peak RSS | 86,261,760 B (**82.3 MB**) |
| Peak footprint | 85,345,600 B (81.4 MB) |
| Solid (k+1)-mers | 13,167,003 |
| Effective clock | ~3.82 GHz (cycles ÷ user-sec), single P-core |

IPC ≈ 2.50 on an **untuned `-O3`** build → headroom likely from `-mcpu=native`/`apple-m*` and the C++23 switch.

## Cache hit rate

**PENDING** — capture with Xcode `xctrace` CPU counters (Xcode.app is installed):

```
xctrace record --template 'CPU Counters' --launch -- \
  build/megahit_core count -k 21 -m 2 --host_mem 61847529062 --mem_flag 1 \
  --output_prefix <prefix> --num_cpu_threads 1 --read_lib_file <reads.lib>
# then: xctrace export … (L1D / L2 load-miss events) → hit rate
```

## Known issues affecting this run

- **Multithreaded CX1 sort crash (arm64):** deterministic data corruption in the parallel `Lv2Sort` /
  edge-writer path with `-t > 1` — trips `assert(snapshot->bucket_id == -1)` at `edge_writer.h:72`
  (Debug) and a wild-address `SIGSEGV` in Release. Forces `-t 1`. Single-thread is clean. This OpenMP
  path is slated for the Boost.Cobalt coroutine rewrite, so it is documented rather than patched.
- macOS Python-driver fix applied (`os.sched_getaffinity` → `_available_cpus()`).

## Tooling used

`/usr/bin/time -l` (IPC via *instructions retired ÷ cycles elapsed*, peak RSS) · MEGAHIT pipeline log
(run length + per-stage) · `xctrace` (cache — pending) · `sample`/`spindump` (hotspots — not yet run).

## Follow-ups (next history entries)

1. **`-mcpu=native` / `-mcpu=apple-m1`+ rebuild + re-profile** → tuned-vs-untuned delta (this baseline is the control).
2. C++23 (`-std=c++2b`) codegen comparison once the build switches.
3. Cache hit rate via `xctrace`.
4. Multithreaded scaling once the CX1 bug is fixed (or after the coroutine rewrite).
5. Full (non-subsampled) SRR341725 profile.

## Artifacts in this directory

- `README.md` — this record.
- `pipeline-stages.log` — raw stage timestamps from the full-pipeline run.
