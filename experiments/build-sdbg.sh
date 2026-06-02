#!/usr/bin/env bash
# Build an experiment bench that links the real SDBG (h11/h12/h13 style).
#
# Why this is separate from build.sh: the SDBG headers pull in src/parallel_hashmap, which uses
# std::result_of (removed in C++20), so SDBG code MUST compile at -std=c++17 — not the c++20/23 that
# build.sh uses for the standalone kernels. We also link the two SDBG translation units the loader needs.
#
# Run from an experiment dir:  ../build-sdbg.sh            (compiles ./bench.cpp -> ./bench)
#                              ../build-sdbg.sh foo.cpp out
set -euo pipefail
EXP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$EXP_ROOT/.." && pwd)"
LLVM="${LLVM:-/opt/homebrew/opt/llvm/bin/clang++}"   # clang 21; AppleClang 15 (LLVM 16) is too old
SDK="$(xcrun --show-sdk-path)"                        # system SDK -> libc++ ABI matches the google-benchmark bottle
P="$(brew --prefix)"
SRC="${1:-bench.cpp}"; OUT="${2:-bench}"
"$LLVM" -std=c++17 -O3 -mcpu=native -isysroot "$SDK" \
  -I "$REPO/src" -I "$P/include" \
  "$SRC" "$REPO/src/sdbg/sdbg_raw_content.cpp" "$REPO/src/sdbg/sdbg_meta.cpp" \
  -L "$P/lib" -lbenchmark -o "$OUT"
echo "built $OUT  [$("$LLVM" --version | head -1)]"
