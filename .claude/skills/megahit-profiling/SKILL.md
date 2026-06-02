---
name: megahit-profiling
description: Profile MEGAHIT assembly runs on macOS / Apple Silicon and record the result in profiling-history/. Use when the user wants to profile, benchmark, or measure megahit performance — run length, instructions-per-cycle (IPC), cache hit rate, peak memory, or function hotspots — or to "note"/"capture"/"record" a profiling run. Covers the macOS PMC tooling (/usr/bin/time -l, xctrace, sample), the build-flag and single-threaded caveats, and the datetime-stamped record format.
---

# MEGAHIT profiling (macOS / Apple Silicon)

Run a performance-profiling pass on this fork and capture it as a datetime-stamped record under
[`profiling-history/`](../../../profiling-history/). One capability: **measure + record**.

## Platform tooling (there is no Linux `perf`)

| Want | Tool | How |
|---|---|---|
| Run length, peak RSS, **IPC** | `/usr/bin/time -l` | reports `instructions retired` + `cycles elapsed` → **IPC = instr ÷ cycles**, plus `maximum resident set size` |
| Cache hit rate | Xcode `xctrace` | `xctrace record --template 'CPU Counters' --launch -- <cmd>` then `xctrace export` the L1D/L2 miss events |
| Function hotspots | `sample` / `spindump` | `sample <pid> 5 -f out.sample` while a stage runs, or `xctrace` Time Profiler |
| Per-stage timing | MEGAHIT log | the wrapper logs each stage timestamp + `ALL DONE. Time elapsed: …` |

## Gotchas (read before measuring)

1. **Measure `megahit_core` *stages* directly, not the `megahit` Python wrapper.** `/usr/bin/time -l` on the
   wrapper reports only the Python process — the assembly work runs in child processes whose rusage is *not*
   included. For IPC / peak-memory, wrap a single `megahit_core <stage>` invocation.
2. **Run length + per-stage breakdown** *do* come from a normal wrapper run (parse its log).
3. **Record the exact build flags.** `-O3` vs Debug, and especially `-mcpu`/`-march`/native tuning, dominate
   results. Pull them with: `grep CXX_FLAGS build/CMakeFiles/megahit_core_no_hw_accel.dir/flags.make`.
4. **Single-threaded for now.** The parallel CX1 sort path crashes on arm64 with `-t > 1` (asserts
   `edge_writer.h:72`; SIGSEGV in Release). Use `-t 1` and record the thread count. IPC/cache are per-core, so
   single-thread is a valid baseline.
5. **Subsample big inputs** for tractable single-threaded runs:
   `gzcat R1.fq.gz | head -n $((4*NREADS)) | gzip > sub_1.fq.gz` (4 lines per read).

## Run a pass

```bash
# 0. Build flags (record these)
grep CXX_FLAGS build/CMakeFiles/megahit_core_no_hw_accel.dir/flags.make
grep CMAKE_BUILD_TYPE build/CMakeCache.txt

# 1. Full pipeline → run length + per-stage breakdown
build/megahit -1 R1.fq.gz -2 R2.fq.gz -o OUT -t 1 > run.log 2>&1
grep -E "Start assembly|k list|Build graph|Assemble contigs|Local assembly|Extract iterative|Merging|contigs,|ALL DONE" run.log

# 2. Dominant stage → IPC + peak memory (build a standalone read lib first)
#    reads.lib is a 2-line-per-library descriptor; for paired plain-text fastq:
#       <abs_R1>,<abs_R2>
#       pe <abs_R1> <abs_R2>
#    (decompress .gz to plain first; the reader expects plain text / FIFOs)
build/megahit_core buildlib reads.lib reads.lib
/usr/bin/time -l build/megahit_core count -k 21 -m 2 --host_mem <bytes> --mem_flag 1 \
  --output_prefix K21 --num_cpu_threads 1 --read_lib_file reads.lib
#    IPC = "instructions retired" / "cycles elapsed"

# 3. Cache hit rate (Xcode installed)
xctrace record --template 'CPU Counters' --launch -- build/megahit_core count … ;  xctrace export …
```

## Record the result

Create a **datetime-stamped directory** and write `record.md` from the template:

```bash
RUN="profiling-history/$(date +%Y-%m-%dT%H%M%S)-<short-label>"
mkdir -p "$RUN"
cp templates/profiling-entry.md "$RUN/record.md"   # then fill it in
# drop raw artifacts alongside: run.log excerpt, time -l output, *.sample, xctrace export
```

Use the template at [`templates/profiling-entry.md`](templates/profiling-entry.md). Then add a row to
[`profiling-history/README.md`](../../../profiling-history/README.md). Convert relative dates to absolute.
Always note any deviation (subsample size, reduced k-list, thread count, build flags) so runs are comparable.

## Reference baseline

`profiling-history/2026-06-01T221830-baseline-single-thread/` — M3 Max, Release `-O3` untuned `gnu++11`,
SRR341725 ×500K pairs, `-t 1`: 250.8 s total; `count` stage IPC ≈ 2.50, peak RSS ≈ 82 MB.
