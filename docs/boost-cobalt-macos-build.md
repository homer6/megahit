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
git clone --depth 1 --branch boost-1.90.0 https://github.com/boostorg/cobalt.git extern/cobalt
```

The compiled runtime is just five files — `extern/cobalt/src/`:

```
channel.cpp   error.cpp   main.cpp   this_thread.cpp   thread.cpp
```

Compile these alongside our driver TUs with `-I/opt/homebrew/include` (system Boost provides Cobalt's header-only deps: Asio, System, MP11, Leaf, Variant2) and link `boost_container` (Cobalt's PMR allocators). Minimal verified pattern:

```sh
clang++ -std=c++2b -stdlib=libc++ -I/opt/homebrew/include \
  our_driver.cpp extern/cobalt/src/*.cpp \
  -L/opt/homebrew/lib -lboost_container -o megahit
```

> **TBD:** Boost's separately-compiled libraries normally want a `BOOST_COBALT_SOURCE`-style define when building their `src/*.cpp` (symbol decl/visibility). The macro wasn't where first expected (`detail/config.hpp`); confirm the exact name/location and whether it's needed for a static in-binary build when wiring the CMake target. Watch for duplicate-symbol or visibility warnings if omitted.

Alternative considered: `FetchContent` of `boostorg/cobalt` at configure time (cleaner provenance, needs network at configure). Vendoring into `extern/` was chosen for a self-contained build; treat `extern/cobalt` as a pinned third-party checkout (gitignore or submodule, TBD).

## Toolchain facts (this machine)

- **Compiler:** AppleClang 15.0.0 (CommandLineTools), arm64. Meets Cobalt's "Clang 16+" floor in practice — coroutines compile and run.
- **C++23 flag:** use **`-std=c++2b`**. AppleClang 15 rejects `-std=c++23` outright. (When setting `CMAKE_CXX_STANDARD 23`, verify CMake emits `c++2b` for this compiler; otherwise pass the flag directly.)
- **`<generator>` (C++23) is NOT in this libc++** — `#include <generator>` fails. Use `cobalt::generator` (which is async and `co_await`-able anyway), not `std::generator`.
- **Boost:** 1.90.0 (`BOOST_VERSION 109000`). `libboost_container` is present and required for Cobalt PMR.
- Cobalt requires **C++20 minimum** and runs all coroutines on a single `asio::io_context`; CPU-bound work must be explicitly offloaded (thread pool) and `co_await`-ed back.
