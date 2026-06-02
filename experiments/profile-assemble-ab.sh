#!/usr/bin/env bash
# A/B the `assemble` stage: scalar vs batched first-sweep filter (the unitig_graph.cpp restructure).
#
# Produces TWO numbers from the same pair of binaries:
#   1. whole-stage wall time (hyperfine, N runs) — the honest end-to-end delta. Expected small: the
#      first-sweep filter is a slice of UnitigGraph construction, which is a slice of `assemble`.
#   2. "time for building" (main_assemble.cpp:166, the UnitigGraph constructor = first sweep + loop sweep)
#      captured per run — the isolated signal closest to the microbench's ~1.25× filter ceiling (h14).
#
# Build the two binaries first (in-tree, no worktree needed):
#   cp build/megahit_core /tmp/mh.batched
#   git show <restructure-commit>^:src/assembly/unitig_graph.cpp > /tmp/uni_scalar.cpp
#   git show HEAD:src/assembly/unitig_graph.cpp                  > /tmp/uni_batched.cpp
#   cp /tmp/uni_scalar.cpp  src/assembly/unitig_graph.cpp && cmake --build build --target megahit_core -j8 && cp build/megahit_core /tmp/mh.scalar
#   cp /tmp/uni_batched.cpp src/assembly/unitig_graph.cpp && cmake --build build --target megahit_core -j8   # restore
#
# Quiet machine, serial. Sample: SRR341725 x500K, k21 graph (30.7 M edges — the largest, first-sweep-dominated).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
SCALAR="${SCALAR:-/tmp/mh.scalar}"
BATCHED="${BATCHED:-/tmp/mh.batched}"
GRAPH="${GRAPH:-profiling/prof_keeptmp/tmp/k21/21}"
RUNS="${RUNS:-6}"
# k21 assemble options as the driver builds them (k_max=99 -> min_standalone 296; k<k_max -> --careful_bubble)
A=(--min_standalone 296 --prune_level 2 --merge_len 20 --merge_similar 0.95 --cleaning_rounds 5
   --disconnect_ratio 0.1 --low_local_ratio 0.2 --min_depth 2 --bubble_level 2 --max_tip_len -1 --careful_bubble)
mkdir -p /tmp/asm_scalar /tmp/asm_batched

echo "### 1. whole-stage wall time (hyperfine, $RUNS runs)"
hyperfine --warmup 1 --runs "$RUNS" \
  -n scalar  "$SCALAR  assemble -s $GRAPH -o /tmp/asm_scalar/21  -t 1 ${A[*]}" \
  -n batched "$BATCHED assemble -s $GRAPH -o /tmp/asm_batched/21 -t 1 ${A[*]}" \
  --export-markdown /tmp/asm_ab.md --export-json /tmp/asm_ab.json

echo
echo "### 2. isolated 'time for building' (UnitigGraph constructor = first sweep), $RUNS runs each"
for bin in scalar batched; do
  path="$SCALAR"; [ "$bin" = batched ] && path="$BATCHED"
  echo "-- $bin --"
  i=1
  while [ "$i" -le "$RUNS" ]; do
    "$path" assemble -s "$GRAPH" -o "/tmp/asm_${bin}/21" -t 1 "${A[@]}" 2>&1 \
      | grep -oE "time for building: [0-9.]+" | head -1
    i=$((i + 1))
  done
done
echo "(compute mean of each group; the batched/scalar ratio is the first-sweep delta)"
