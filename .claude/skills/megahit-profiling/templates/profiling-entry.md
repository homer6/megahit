<!-- This file is the run directory's README.md (renders when the dir is opened). -->
# Profiling run — <YYYY-MM-DDThh:mm:ss> — <short label>

<1–2 lines: what this run is, why, any deviation from a standard run.>

| Field | Value |
|---|---|
| Date/time | <absolute local datetime> |
| Git | `<short-sha>` (branch `<branch>`) |
| Machine | Apple <chip> (<hw.model>) — <P>+<E> cores, <RAM> |
| OS / compiler | macOS <ver> · AppleClang <ver> |
| Sample | <accession> — catalog: [`../samples/<accession>.md`](../samples/<accession>.md) · subsample: <derivation + md5> |
| Threads | <n> |

> Results are specific to this sample+subsample — only comparable against runs using the same one.

## Build (the binary these numbers came from)

`<build-dir>` — **<Release/Debug>**, `CMAKE_BUILD_TYPE=<…>`. Exact `CXX_FLAGS`:

```
<paste from grep CXX_FLAGS build/CMakeFiles/megahit_core_no_hw_accel.dir/flags.make>
```

- Optimization: `<-O3 / -O1 / …>`  · Native tuning: `<-mcpu=… / none>`  · Std: `<gnu++11 / c++2b>`
- <other notable flags / driver fixes>

## Full pipeline — run length

```
<exact megahit command>
```

- **Total wall: <s>**
- k-list: <…>
- Output: <contigs>, <bp>, N50 <bp> (max <bp>)

### Phase breakdown

| Phase | ~Time | Share |
|---|---:|---:|
| <…> | | |

## Stage micro-profile — `<stage>` (IPC + memory)

```
<exact /usr/bin/time -l megahit_core <stage> command>
```

| Metric | Value |
|---|---|
| Wall | <s> |
| Instructions retired | <n> |
| Cycles elapsed | <n> |
| **IPC** | **<instr/cycles>** |
| Peak RSS | <MB> |

## Cache hit rate

<xctrace L1D/L2 hit rate, or **PENDING** with the command to run>

## Notes / observations / follow-ups

- <…>

## Artifacts in this directory

- `<file>` — <what it is>
