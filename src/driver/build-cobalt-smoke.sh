#!/usr/bin/env bash
# Build the Boost.Cobalt bring-up smoke test (src/driver/cobalt_smoke.cpp).
#
# Compiles Cobalt's 5 vendored runtime TUs (modules/cobalt/src/*.cpp) straight into the binary — Homebrew
# Boost 1.90 ships Cobalt *headers* but no compiled libboost_cobalt (docs/boost-cobalt-macos-build.md).
# Vendored Cobalt headers come FIRST on the include path (offline, version-pinned); the system Boost prefix
# supplies Cobalt's header-only deps (Asio, System, MP11, Leaf, Variant2) and the compiled libboost_container.
#
# Toolchain: Homebrew clang (C++23) — the newer compiler intended for the C++23 driver. AppleClang 15 also
# works with -std=c++2b (see the build doc).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LLVM="${LLVM:-/opt/homebrew/opt/llvm/bin/clang++}"
SDK="$(xcrun --show-sdk-path)"
P="$(brew --prefix)"
OUT="${1:-/tmp/cobalt_smoke}"

if [ ! -e "$REPO/modules/cobalt/src/main.cpp" ]; then
  echo "modules/cobalt missing — run: git submodule update --init --recursive" >&2
  exit 1
fi

# Cobalt's CORE runtime = 7 TUs (per modules/cobalt/CMakeLists.txt) — the top-level src/*.cpp PLUS
# src/detail/{exception,util}.cpp. (src/io/*.cpp is a separate networking target needing SSL etc. — not
# needed to orchestrate stages.) Cobalt's own build defines BOOST_COBALT_SOURCE=1 (private to its src) and
# BOOST_COBALT_USE_BOOST_CONTAINER_PMR=1 (public, hence -lboost_container). For a static in-binary build
# BOOST_COBALT_DECL is empty regardless of SOURCE, so a single invocation with both defines is fine.
COBALT_SRC=("$REPO"/modules/cobalt/src/*.cpp "$REPO"/modules/cobalt/src/detail/*.cpp)

"$LLVM" -std=c++23 -O2 -stdlib=libc++ -isysroot "$SDK" \
  -DBOOST_COBALT_SOURCE=1 -DBOOST_COBALT_USE_BOOST_CONTAINER_PMR=1 \
  -I "$REPO/modules/cobalt/include" -I "$P/include" \
  "$REPO/src/driver/cobalt_smoke.cpp" "${COBALT_SRC[@]}" \
  -L "$P/lib" -lboost_container -lboost_system -o "$OUT"
echo "built $OUT  [$("$LLVM" --version | head -1)]"
