#!/usr/bin/env bash
# Standard experiment build — modern toolchain (Homebrew clang 21), Release, native-tuned.
# Run from an experiment dir:  ../build.sh   (or ../build.sh other.cpp out)
#   STD=c++23 ../build.sh   to test the C++23 target standard.
set -euo pipefail
EXP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM="${LLVM:-/opt/homebrew/opt/llvm/bin/clang++}"   # clang 21; AppleClang 15 is too old (LLVM 16)
SDK="$(xcrun --show-sdk-path)"                         # system SDK -> libc++ ABI matches the google-benchmark bottle
P="$(brew --prefix)"
SRC="${1:-bench.cpp}"; OUT="${2:-bench}"; STD="${STD:-c++20}"
"$LLVM" -std="$STD" -O3 -mcpu=native -isysroot "$SDK" \
  -I "$EXP_ROOT" -I "$EXP_ROOT/../src" -I "$P/include" \
  "$SRC" -L "$P/lib" -lbenchmark -o "$OUT"
echo "built $OUT  [$("$LLVM" --version | head -1)]"
