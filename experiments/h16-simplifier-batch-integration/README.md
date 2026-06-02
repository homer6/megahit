# H16 — batching the rest of `assemble` (simplifiers + SDBG pruning): **DISPROVEN — it regresses 1.41×**

## Hypothesis

After the first sweep (h11–h14, ~1.05× on the stage), batch the *other* rank/select sweeps in `assemble` —
`tip_remover`, `low_depth_remover`, `weak_link_remover`, `bubble_remover` (collapsed unitig graph) and
`sdbg_pruning::RemoveTips`/`Trim` (the raw SDBG) — using the h15 foundation primitives. Prediction (going in):
small whole-stage gain, since most of these run on the 150 K-vertex collapsed graph (vs 30.7 M raw edges).

## What was built (all `== scalar`, bit-identical contigs, then reverted)

Foundation (h15): `OutgoingEdgesBatch`/`IncomingEdgesBatch`/`EdgeOutdegreeZeroBatch`/`EdgeIndegreeZeroBatch`
(2.05× / 1.89× in isolation). Wired into all 5 sites via two-phase collect→batch→replay, each gated
**bit-identical** by [`../verify-contigs.sh`](../verify-contigs.sh) (md5 `bf2c562…` — every site PASSED). So
correctness was never the problem; **performance was.**

## Evidence — M3 Max, clang 21, real k21 assemble, hyperfine (quiet machine)

| build | assemble (k21) | vs scalar |
|---|---:|---:|
| fully scalar | 38.75 s ± 0.09 | 1.00× |
| **first-sweep only** | **36.5 s** | **1.05× faster** ✅ |
| **+ all simplifier/pruning batching** | **54.58 s ± 0.06** | **1.41× SLOWER** ❌ |

Per-phase localization (one run each) pins the **entire** regression to `sdbg_pruning` (site #5):

| phase | scalar | fully-batched |
|---|---:|---:|
| `sdbg_pruning` "Tips removal" (raw SDBG) | 15.3 s | **31.5 s** (+16.3 s) |
| first-sweep "time for building" | 13.7 s | 12.7 s (slight win) |
| collapsed-graph cleaning rounds | ~10 s | ~10 s (neutral) |

## Why it regresses

`sdbg_pruning::Trim` runs ~6× (tip length 2→4→…→42); each call now radix-sorts **all 30.7 M edges twice**
for the degree-zero entry filter. But the scalar code only evaluates the *cheap, early-exiting* degree-zero
check on the **shrinking** non-`ignored` set, and short-circuits. The batch's radix-sort overhead × repeated
full-graph passes × over-compute (every edge, every round) = **+16 s for zero benefit**. The collapsed-graph
simplifiers (low-depth/tip/weak-link/bubble) net **neutral** — too few elements (150 K) and too many repeated
batches (low-depth's geometric loop) for the sort to pay.

## Verdict — **DISPROVEN; reverted.** The first sweep was the only real win.

This is the measurement-wins discipline doing its job: the [204×-smaller-collapsed-graph analysis]
predicted ~1% / high-risk; the repeated full-graph site (`sdbg_pruning`) turned it into a **41% regression**.
Reverted sites #1/#2/#4/#5/#6 (and the h15 foundation primitives, now unused); kept the first-sweep batch
(`unitig_graph.cpp` `is_path_end`, the proven 1.05×). **Lesson for batched rank/select: it pays only on a
*large, single-pass* scatter (the first sweep over 30.7 M raw edges); it loses on small/collapsed sweeps and
on *repeated* full-graph passes where the per-element work is cheap and early-exiting.**

Artifacts: [`assemble-ab.md`](assemble-ab.md) (the hyperfine A/B), per-phase log in this dir.
