# Boost.Cobalt on macOS / Apple Silicon — build integration notes

> **Provenance / status.** Engineering findings from this fork's bring-up, captured 2026-06-01 on macOS (Apple Silicon, arm64), Homebrew toolchain. These are decisions/observations for wiring Boost.Cobalt into the C++23 driver, not upstream documentation. Verify the one item marked **TBD** when the CMake target is wired.

## Context

The plan is to replace the Python pipeline driver (`src/megahit`) with a single-process **C++23 + Boost.Cobalt coroutine** orchestrator: each assembly stage (`buildlib`, `count`, `read2sdbg`, `seq2sdbg`, `iterate`, `assemble`, `local`) is sequenced as a `co_await`-ed coroutine task in one process — no `fork`/`exec`. Cobalt is **single-threaded by design**, so it orchestrates the pipeline; the data-parallel work *inside* each stage stays parallel (OpenMP today, an `asio::thread_pool` offload later). See [`boost-capy-and-physical-design.md`](boost-capy-and-physical-design.md) for the broader coroutine direction (Capy is the longer-term foundation; Cobalt is the released, installable library we can build against today).

## The obstacle: Homebrew Boost ships Cobalt headers, not its compiled library

Boost.Cobalt is **not header-only** — it has a small separately-compiled runtime. Homebrew's `boost 1.90.0_1` installs the Cobalt **headers** but **not** a compiled `libboost_cobalt`:

- `/opt/homebrew/include/boost/cobalt.hpp` and `/opt/homebrew/include/boost/cobalt/` — present.
- `/opt/homebrew/lib/libboost_cobalt.*` — **absent** (80 other `libboost_*` libs are there; `cobalt` is not among them, and `find` across the prefix turns up only headers).
- There is **no** `boost/cobalt/src.hpp` single-header-impl include either.

Consequently `find_package(Boost COMPONENTS cobalt)` + `Boost::cobalt`, or a plain `-lboost_cobalt`, **cannot link** in this environment.

## The fix: vendor Cobalt's `src/*.cpp` and compile it in-tree

Vendor the Cobalt sources at the **matching tag** so they agree with the installed headers, and compile them straight into the `megahit` binary against the system Boost headers:

```sh
git submodule add https://github.com/boostorg/cobalt.git modules/cobalt
git -C modules/cobalt checkout boost-1.90.0   # pin to the tag matching the installed Boost headers
git add modules/cobalt .gitmodules
# on fresh checkouts:  git submodule update --init --recursive
```

The compiled runtime is **seven** files (per `modules/cobalt/CMakeLists.txt`'s `boost_cobalt` target) — the
top-level `src/*.cpp` **plus `src/detail/`** (the original "five files" note missed these — `exception.cpp`
defines `already_awaited`/`throw_bad_executor`/`completed_unexpected`, so omitting it fails to link):

```
src/detail/exception.cpp  src/detail/util.cpp
src/channel.cpp  src/error.cpp  src/main.cpp  src/this_thread.cpp  src/thread.cpp
```

(`src/io/*.cpp` is a **separate** `boost_cobalt_io` target — networking/timers, needs OpenSSL etc. — and is
**not** needed to orchestrate stages.) **Verified, working** pattern (see `src/driver/build-cobalt-smoke.sh`
and the runnable proof `src/driver/cobalt_smoke.cpp`):

```sh
clang++ -std=c++2b -stdlib=libc++ -isysroot "$(xcrun --show-sdk-path)" \
  -DBOOST_COBALT_SOURCE=1 -DBOOST_COBALT_USE_BOOST_CONTAINER_PMR=1 \
  -I modules/cobalt/include -I /opt/homebrew/include \
  our_driver.cpp modules/cobalt/src/*.cpp modules/cobalt/src/detail/*.cpp \
  -L /opt/homebrew/lib -lboost_container -o megahit
```

> **TBD → RESOLVED.** Two defines are needed (both from Cobalt's own CMake): **`BOOST_COBALT_SOURCE=1`**
> (Cobalt sets it `PRIVATE` on its `src/*.cpp`; on a static in-binary build `BOOST_COBALT_DECL` is empty
> regardless, so applying it to the whole single-invocation build is harmless) and
> **`BOOST_COBALT_USE_BOOST_CONTAINER_PMR=1`** (`PUBLIC`; selects boost::container PMR → the `-lboost_container`).
> **Do NOT link `boost_system`** — it is header-only in Boost 1.90 (no `libboost_system` exists); `boost_container`
> is the only Boost lib needed. No duplicate-symbol/visibility issues observed.

`boostorg/cobalt` is pinned as a **git submodule** at `modules/cobalt` (tag `boost-1.90.0`, recorded in `.gitmodules`), so its version tracks the installed Boost headers and fresh clones pick it up via `git submodule update --init --recursive`. (`FetchContent` was the alternative — rejected to keep the dependency explicit and offline-buildable once checked out.)

## Toolchain facts (this machine)

- **Compiler: use AppleClang + system libc++, NOT Homebrew clang.** AppleClang 15.0.0 (CommandLineTools),
  arm64, compiles and runs Cobalt's coroutines (meets the "Clang 16+" floor in practice). Building the
  smoke test with **Homebrew clang 21 fails to link** — its newer libc++ *headers* reference libc++abi
  symbols absent from the system SDK (`__cxa_init_primary_exception`,
  `std::exception_ptr::__from_native_exception_pointer`). More importantly, **Homebrew's Boost libs are built
  against the system libc++ ABI**, and `megahit_core` builds with AppleClang too — so the whole C++23 megahit
  (driver + core + Boost) must share **one** libc++. AppleClang/system-libc++ is that toolchain.
- **C++23 flag:** AppleClang emits **`-std=gnu++2b`** for `CMAKE_CXX_STANDARD 23` (it rejects the literal
  `c++23`); that *is* C++23 mode. The whole project is now `CMAKE_CXX_STANDARD 23` (see `CMakeLists.txt`); the
  vendored-dep C++23 patches are noted in `parallel_hashmap/phmap*.h` and `idba/hash.h` (`[megahit C++23 patch]`).
- **`<generator>` (C++23) is NOT in this libc++** — `#include <generator>` fails. Use `cobalt::generator` (which is async and `co_await`-able anyway), not `std::generator`.
- **Boost:** 1.90.0 (`BOOST_VERSION 109000`). `libboost_container` is present and required for Cobalt PMR.
- Cobalt requires **C++20 minimum** and runs all coroutines on a single `asio::io_context`; CPU-bound work must be explicitly offloaded (thread pool) and `co_await`-ed back.
