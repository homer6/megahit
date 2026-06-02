# H11 — `SDBG::ForwardBatch` on the real graph (integration foundation)

**Goal:** prove the batched-select lever on the *actual* SdBG navigation primitive `Forward` (= the
`rank`+`select` hammered by the `assemble` sweep), on a real graph — the foundation for wiring batched
rank/select into the unitig-graph build (#4).

**Method:** `bench.cpp` loads the **real subsample-scale k21 SdBG** (`LoadFromFile`, **30.7 M edges, 61 MB >
L2**) and runs 2M random `Forward`. `ForwardBatch` (added to `src/sdbg/sdbg.h`) computes each select argument
inline (`a=GetW`, `count=rs_w_.rank`, cache-resident) and resolves the **scattered `rs_last_.select` in one
radix-sorted batch** (`select_batch`). Correctness: `ForwardBatch == Forward` over all 2M (**PASS**).

> Toolchain note: this TU is **C++17** — `sdbg.h` pulls in vendored `parallel_hashmap`, which uses
> `std::result_of` (removed in C++20). The standalone kernel experiments stay C++20/26; the API itself is C++11-clean.

## Evidence — M3 Max, clang 21, 2026-06-02 (2M `Forward`, 5 reps)

| | time | speedup |
|---|---:|---:|
| `Forward` per-call (scalar) | 239 ms | 1.00× |
| `ForwardBatch` (radix-sorted select) | **127 ms** | **1.88×** |

## Verdict — **PROVEN** on the real SDBG

The batched primitive delivers **1.88×** on real graph navigation, correctness-gated. It's **1.88× not 2.88×**
because `Forward` = `rank`+`GetW` (inline, cache-resident) **+** `select` (batched) — only the scattered select
is accelerated, so Amdahl caps it. `megahit_core` rebuilds clean with the new method.

## What's delivered vs what's left

**Delivered (in the real headers, correctness-gated):**
- `RankAndSelect::select_batch` (radix) **2.88×**, `rank_batch_prefetch` **1.52×** (`kmrns.h`).
- `SDBG::ForwardBatch` **1.88×** on the real graph (`sdbg.h`).

**Left (the end-to-end `assemble` speedup):** wire `ForwardBatch` into the unitig-graph build's first sweep
(`unitig_graph.cpp`). That sweep is a *fused* filter (`NextSimplePathEdge`) + path-walk + `EdgeReverseComplement`
loop, so it needs restructuring into **collect-frontier → batch-resolve → process** phases, validated
**bit-identical via `megahit --test`** at each step. That's the careful, correctness-critical part of #4 — done
incrementally, not in one shot.
