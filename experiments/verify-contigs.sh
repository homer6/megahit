#!/usr/bin/env bash
# Contig-identity regression gate for the SdBG integration experiments (h11/h12/h13...).
#
# Every batched-primitive change to src/sdbg/sdbg.h must be behavior-preserving. This rebuilds
# megahit_core, runs the standard 500K-pair pipeline single-threaded, and asserts final.contigs.fa is
# byte-identical (md5) to the committed baseline. A literal code move (e.g. ComputeOutgoings ->
# ComputeOutgoingsFrom) must produce IDENTICAL contigs; anything else means the refactor changed behavior.
#
# Usage:  experiments/verify-contigs.sh [OUT_DIR] [BASELINE_MD5]
#   OUT_DIR       default profiling/prof_verify2
#   BASELINE_MD5  default = the committed -t1 500K baseline (matches profiling/prof_out/final.contigs.fa)
#
# Sample: SRR341725 x500K pairs, k-list 21,29,39,59,79,99, -t 1. Only comparable at this exact sample.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
OUT="${1:-profiling/prof_verify2}"
BASE="${2:-bf2c56284bcd0a91c02448ae3f6b35f2}"
R1=profiling/data/sub_1.fq.gz
R2=profiling/data/sub_2.fq.gz

echo "=== rebuild megahit_core ==="
cmake --build build --target megahit_core -j8 2>&1 | tail -3
echo "BUILD OK"

rm -rf "$OUT"
echo "=== run 500K pipeline -t 1 -> $OUT ==="
build/megahit -1 "$R1" -2 "$R2" -o "$OUT" -t 1 >/tmp/verify_run.log 2>&1 && echo "run exit=0" || { echo "run FAILED"; tail -20 /tmp/verify_run.log; exit 1; }

NOW=$(md5 -q "$OUT/final.contigs.fa")
echo "BASELINE md5: $BASE"
echo "NOW      md5: $NOW"
grep -E "contigs," /tmp/verify_run.log | tail -1
if [ "$BASE" = "$NOW" ]; then echo "RESULT: IDENTICAL (behavior-preserving)"; else echo "RESULT: DIFFER !!! (refactor changed behavior)"; exit 1; fi
