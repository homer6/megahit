# profiling-history

Committed record of MEGAHIT performance-profiling runs for this macOS / Apple-Silicon fork. One
**datetime-stamped directory per run**, each with its own **`README.md`** record plus raw capture
artifacts. (Raw inputs/large outputs live in the gitignored `profiling/`; only the *notes* are tracked.)

**To run and record a pass, use the [`megahit-profiling`](../.claude/skills/megahit-profiling/SKILL.md) skill.**

## Layout / naming

```
profiling-history/
  README.md                                   ← this index
  samples/                                     ← data-sample catalog (see below)
    README.md  SRR341725.md  …
  <YYYY-MM-DDThhmmss>-<label>/                ← one dir per run, datetime-stamped
    README.md                                  ← the run record (renders when you open the dir)
    pipeline-stages.log                        ← raw artifacts (logs, time -l output, xctrace export, …)
```

Generate the stamp with `date +%Y-%m-%dT%H%M%S`. Every record states: machine, **exact build flags**,
the **sample (link to its catalog doc) + subsample**, thread count, run length, per-stage breakdown, IPC,
cache, peak memory, hotspots.

## Methodology (macOS / Apple Silicon)

- No Linux `perf`. Use `/usr/bin/time -l` (wall, max RSS, **instructions retired + cycles elapsed → IPC**),
  Xcode `xctrace` (CPU counters → cache events), `sample` / `spindump` (function hotspots).
- **Measure `megahit_core` *stages* directly**, not the `megahit` Python wrapper — `time -l` on the wrapper
  only counts the Python process; the real work runs in child processes whose rusage is not included.
- Run length + per-stage timing come from the wrapper run log ("Time elapsed" / "ALL DONE").
- **Record the build flags** — `-O3` vs debug, and especially `-mcpu`/native tuning, change everything.
- **Single-threaded for now** (parallel CX1 sort bug on arm64, `-t > 1`); always note the thread count.
- Subsample large inputs for tractable single-threaded runs.

## Samples

**Profiling is highly sample-dependent** (organism mix, read length, coverage, GC all shift results).
Each sample is catalogued in [`samples/`](samples/README.md) with source, characteristics, and checksums.
**Only compare runs that used the same sample + subsample.**

## Runs

| When | Label | Machine | Build | Sample | Thr | Wall | IPC | Notes |
|---|---|---|---|---|---:|---:|---:|---|
| [2026-06-01T22:18](2026-06-01T221830-baseline-single-thread/README.md) | baseline single-thread | M3 Max | Release `-O3`, untuned, gnu++11 | [SRR341725](samples/SRR341725.md) ×500K pairs | 1 | 250.8 s | 2.50 (count) | first baseline; cache pending; `-mcpu` untuned (control) |
| [2026-06-02T03:47](2026-06-02T034721-first-sweep-batched-assemble/README.md) | first-sweep batched `assemble` | M3 Max | Release `-O3`, untuned, gnu++11 (same as baseline) | [SRR341725](samples/SRR341725.md) ×500K pairs | 1 | 240.1 s | 2.50 (count, unchanged) | **`assemble` 1.05× faster** (hyperfine 36.5 vs 38.3 s), contigs md5-identical. Batching the *rest* of `assemble` was **1.41× slower → reverted** (see [h16](../experiments/h16-simplifier-batch-integration/README.md)) |
