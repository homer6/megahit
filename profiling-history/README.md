# profiling-history

Committed record of MEGAHIT performance-profiling runs for this macOS / Apple-Silicon fork. One
**datetime-stamped directory per run** holds the structured `record.md` plus raw capture artifacts.
(Raw inputs/large outputs live in the gitignored `profiling/`; only the *notes* are tracked here.)

**To run and record a pass, use the [`megahit-profiling`](../.claude/skills/megahit-profiling/SKILL.md) skill.**

## Layout / naming

```
profiling-history/
  README.md                                   ← this index
  <YYYY-MM-DDThhmmss>-<label>/                ← one dir per run, datetime-stamped
    record.md                                 ← structured record (see the skill's template)
    pipeline-stages.log                        ← raw artifacts (logs, time -l output, …)
```

Generate the stamp with `date +%Y-%m-%dT%H%M%S`. Always record: machine, **exact build flags**,
sample (+subsample), thread count, run length, per-stage breakdown, IPC, cache, peak memory, hotspots.

## Methodology (macOS / Apple Silicon)

- No Linux `perf`. Use `/usr/bin/time -l` (wall, max RSS, **instructions retired + cycles elapsed → IPC**),
  Xcode `xctrace` (CPU counters → cache events), `sample` / `spindump` (function hotspots).
- **Measure `megahit_core` *stages* directly**, not the `megahit` Python wrapper — `time -l` on the wrapper
  only counts the Python process; the real work runs in child processes whose rusage is not included.
- Run length + per-stage timing come from the wrapper run log ("Time elapsed" / "ALL DONE").
- **Record the build flags** — `-O3` vs debug, and especially `-mcpu`/native tuning, change everything.
- **Single-threaded for now** (parallel CX1 sort bug on arm64, `-t > 1`); always note the thread count.
- Subsample large inputs so a single-threaded run is tractable.

## Runs

| When | Label | Machine | Build | Input | Thr | Wall | IPC | Notes |
|---|---|---|---|---|---:|---:|---:|---|
| [2026-06-01T22:18](2026-06-01T221830-baseline-single-thread/record.md) | baseline single-thread | M3 Max | Release `-O3`, untuned, gnu++11 | SRR341725 ×500K pairs | 1 | 250.8 s | 2.50 (count) | first baseline; cache pending; `-mcpu` untuned (control) |
