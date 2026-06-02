#!/usr/bin/env bash
# Build the Boost.Cobalt bring-up smoke test (src/driver/cobalt_smoke.cpp).
#
# Compiles Cobalt's 5 vendored runtime TUs (modules/cobalt/src/*.cpp) straight into the binary — Homebrew
# Boost 1.90 ships Cobalt *headers* but no compiled libboost_cobalt (docs/boost-cobalt-macos-build.md).
# Vendored Cobalt headers come FIRST on the include path (offline, version-pinned); the system Boost prefix
# supplies Cobalt's header-only deps (Asio, System, MP11, Leaf, Variant2) and the compiled libboost_container.
#
# Toolchain: **system AppleClang + system libc++** — NOT Homebrew clang. Homebrew's Boost libs (libboost_*)
# are built against the system libc++ ABI, and `megahit_core` also builds with AppleClang; using Homebrew
# clang's newer libc++ here instead causes undefined libc++abi symbols (__cxa_init_primary_exception, etc.)
# and would ABI-mismatch the Boost libs. AppleClang 15 compiles Cobalt's coroutines fine at -std=c++2b
# (== C++23 mode; AppleClang rejects the literal `c++23`). Override with CXX=... STD=... if needed.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LLVM="${CXX:-$(xcrun -f clang++)}"
STD="${STD:-c++2b}"
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

"$LLVM" -std="$STD" -O2 -stdlib=libc++ -isysroot "$SDK" \
  -DBOOST_COBALT_SOURCE=1 -DBOOST_COBALT_USE_BOOST_CONTAINER_PMR=1 \
  -I "$REPO/modules/cobalt/include" -I "$P/include" \
  "$REPO/src/driver/cobalt_smoke.cpp" "${COBALT_SRC[@]}" \
  -L "$P/lib" -lboost_container -o "$OUT"
echo "built $OUT  [$("$LLVM" --version | head -1)]"
