# Seastar — framework guide for the MEGAHIT conversion

> **Decision / status.** This fork is being converted to a [Seastar](https://github.com/scylladb/seastar)
> application (ScyllaDB's shard-per-core C++ framework), **using the modern C++20-coroutine API** (`co_await`/
> `co_return`/`seastar::future<T>`), *not* the legacy `.then()` continuation API. Plan: stand up a new Seastar
> base, then migrate the existing pipeline onto it. This **supersedes the earlier Boost.Cobalt + thread-pool
> direction** ([`coroutine-parallelism-architecture.md`](coroutine-parallelism-architecture.md) — keep its
> *measured findings* and the CX1 bug root-cause, but the substrate is now Seastar shards, not `asio::thread_pool`).
> Captured 2026-06-02. This is a navigational map; ground every API against current upstream docs before relying on it.

---

## 0. Read this first — platform reality & architectural fit (the two load-bearing decisions)

Two facts decide whether this conversion is viable. Resolve them *before* large-scale migration.

### A. Seastar is Linux-only. This is a macOS / Apple Silicon fork.

Seastar has **no native macOS support** — it targets **Linux or OSv** and depends on Linux-specific machinery
(epoll/`io_uring`, Linux AIO, `eventfd`/`timerfd`, hugepages, optional DPDK). There is no Darwin/arm64 reactor.
([scylladb/seastar#112](https://github.com/scylladb/seastar/issues/112), [repo README](https://github.com/scylladb/seastar),
[seastar.io](https://seastar.io/).) Practical options:

1. **Develop/run in a Linux container** (Docker) on the Mac — works, but you lose native Apple-Silicon execution
   and the unified-memory/NEON/MLX angles this fork has been profiling; perf numbers no longer reflect the M-series host.
2. **Re-target production to Linux** (the M-series Mac becomes a dev box; Seastar runs on Linux servers). This
   is the honest Seastar-native path, but it changes the project's "macOS/Apple Silicon" premise.
3. **Linux/arm64** (e.g. Graviton, or an arm64 Linux VM on the Mac) keeps arm64 but on Linux's syscalls.

**This must be decided explicitly** — the bulk of this repo's measured work (IPC/cache/bandwidth on M3 Max, the
NEON/MLX analyses) assumes a native macOS/Apple-Silicon target that Seastar does not run on.

### B. Seastar is a *server* framework; MEGAHIT is a *batch-compute* job over a *shared* graph.

Seastar's core bet is **shared-nothing, shard-per-core**: each core owns private memory, *no shared mutable
state*, cross-core work is explicit message-passing (`smp::submit_to`). That's ideal for request-parallel
servers (Scylla, Redpanda). MEGAHIT is the opposite shape: a single large **shared** succinct-dBG that
shared-memory data-parallel loops (CX1 sort, rank/select traversal, graph simplification) sweep with
`#pragma omp parallel for`. The tension:

- The SdBG (one big structure) would have to be **partitioned across shards**, and any rank/select navigation
  that crosses a partition boundary becomes a **cross-shard message** (round-trip latency) instead of a memory
  load. For a pointer-chasing graph traversal (the 56% `assemble` stage, already latency-bound), shard-crossing
  could be *much worse* than shared memory. Partitioning the dBG so traversals stay shard-local is a hard,
  open research question, not a port.
- Seastar's headline features (async networking, RPC, HTTP, the I/O scheduler for many concurrent requests) are
  largely irrelevant to a single-shot assembler. The parts that matter are the **reactor + coroutine scheduler +
  per-shard execution**; you're adopting a large runtime for a subset of its value.

**The honest framing:** Seastar gives a clean coroutine model and per-core execution, but its shared-nothing
constraint fights MEGAHIT's shared-graph algorithm. The migration's central design problem is **how to shard the
SdBG so per-shard work is independent** — or whether to keep the graph on one shard (losing the multicore win
that motivated the whole effort). Prototype this (a sharded SdBG load + a cross-shard-free traversal microbench)
**before** committing the codebase. See [§11](#11-megahit-conversion-sketch).

---

## 1. Core architecture

**Shard-per-core / shared-nothing.** One OS thread pinned per core (a *shard*); each shard has private memory
(pre-allocated from the local NUMA node at startup) and its own event loop. No shared state between shards; no
locks. `seastar::this_shard_id()` = current shard, `seastar::smp::count` = shard count. **Implication:** data on
the wrong core must be reached by message-passing, never a shared pointer.

**The reactor (event loop).** Each shard polls completed I/O, dispatches ready coroutines, and enforces a
**500 µs task quota** — a task that runs longer without yielding is a *reactor stall* (>20 ms is significant).
**Never block the reactor**: no `sleep()`, mutex, `fread()`, `mmap`'d access — any blocking call starves every
task on that core.

**Memory.** Seastar takes nearly all RAM at startup, split per shard (`~(total − reserved)/smp::count`); OS
reserve = `max(1.5 GB, 7%)`. Tune with `-m` (total) and `--reserve-memory`. Each shard uses its own allocator —
**a `std::shared_ptr` destructing on the wrong shard is a bug** (use `foreign_ptr<>` or redesign ownership).

---

## 2. Coroutines — the modern API (use this, not `.then()`)

C++20 coroutines are the **preferred** way to write Seastar code; they replace continuation chains, keep locals
alive across suspension, allow normal `try/catch`, read top-to-bottom, and insert preemption checks at each
`co_await`. A coroutine is any function returning `seastar::future<T>` that uses `co_await`/`co_return`.

```cpp
#include <seastar/core/coroutine.hh>
seastar::future<int> slow_fetch_and_increment() {
    auto n = co_await read();        // suspends if not ready; n preserved across suspension
    co_await seastar::sleep(1s);     // yields the reactor; n still alive
    co_await write(n + 1);
    co_return n;
}
```

**Preemption / yielding.** Every `co_await` checks the task quota and may yield. For CPU-bound loops *with no
I/O* (most of MEGAHIT's inner work), insert explicit yield points or you stall the reactor:

```cpp
#include <seastar/coroutine/maybe_yield.hh>
for (...) { compute(); co_await seastar::coroutine::maybe_yield(); }
```

For tight always-ready loops where a yield would be wrong, `seastar::coroutine::without_preemption_check(...)`.
*(MEGAHIT note: the 30.7 M-edge / millions-of-reads inner loops are exactly the "long CPU loop" case — they need
periodic `maybe_yield()`, or they must run as bounded chunks. This is the Seastar analogue of the "coarse
granularity" rule.)*

**Concurrency inside a coroutine** (`co_await` is sequential):
- `seastar::coroutine::all([]{return a();}, []{return b();})` — fixed-arity parallel join (structured bindings).
- `seastar::coroutine::parallel_for_each(range, [](auto& x) -> seastar::future<> { ... })` — dynamic fan-out
  (lambda-coroutine-safe). **This is the Seastar replacement for `#pragma omp parallel for`** — but it fans out
  *within one shard's reactor*, not across cores; true multicore parallelism comes from `sharded<T>` +
  `invoke_on_all` ([§7](#7-inter-shard-communication)), i.e. partition the work across shards.

**Exceptions** translate to/from exceptional futures automatically (`try/catch` works). Perf-sensitive paths
avoid rethrow with `co_return seastar::coroutine::exception(eptr)`.

**Generators** (`co_yield`, experimental): `seastar::coroutine::experimental::generator<T, Container>` with a
bounded buffer + back-pressure — a producer/consumer stream (e.g. yielding read batches or k-mer windows).

### The lambda-coroutine fiasco (the #1 footgun)

A lambda coroutine used as a `.then()` continuation captures by reference, but `.then()` destroys the lambda
before the coroutine resumes → **use-after-free**. Fixes: wrap with `seastar::coroutine::lambda(...)`, or give
the lambda a **named lvalue** whose lifetime the enclosing frame keeps alive. Related: **reference parameters to
a coroutine dangle** if the caller's scope ends before a `co_await` resumes — pass by value or `do_with`.

---

## 3. Futures / continuations (legacy — context only)

`seastar::future<T>` = a maybe-not-ready value (pending / ready-with-value / ready-with-exception);
`make_ready_future<T>(v)` for sync fast paths. `.then()` chains continuations. Combinators still compose with
coroutines: `when_all` / `when_all_succeed`, `parallel_for_each`, `map_reduce`, `do_until(cond,body)`,
`repeat`/`keep_doing`. **Prefer coroutines for all new code**; reach for combinators only where they read cleaner.

---

## 4. Lifetime management

- **`do_with(obj, [](auto& o){...})`** — heap-allocates `obj`, keeps it alive until the future resolves. Less
  needed in coroutines (locals live in the frame), but essential in continuation code. *Omitting `&` copies the
  object → destroyed while the future is live (bug).*
- **`seastar::gate` + `with_gate(gate_, fiber)`** — reference-counted barrier for background fibers;
  `gate_.close()` resolves only when every fiber that entered has finished. **Every service class's `stop()`
  closes its gate.** Fire-and-forget futures without a gate can outlive the objects they reference.
- **`seastar::abort_source`** — cancellation; `request_abort()` fires registered callbacks. Must be passed
  explicitly down the call chain (no implicit propagation).

---

## 5. Same-shard fiber coordination

| Need | API |
|---|---|
| Mutual exclusion between fibers | `seastar::semaphore`, `seastar::shared_mutex` |
| Async condition variable | `seastar::condition_variable` |
| Producer/consumer pipe | `seastar::pipe<T>` |
| Async loops | `keep_doing()`/`repeat()`, `do_until(cond, body)` |
| Background-fiber shutdown | `seastar::gate` |
| Lifetime tie-in | `do_with()` |
| Cancellation | `seastar::abort_source` |

---

## 6. I/O

**Disk** goes through Seastar's DMA storage API (zero-copy, governed by the I/O scheduler) — **never blocking
`fread`/`mmap`** (reactor stall). This is the relevant subsystem for MEGAHIT's read input + contig/edge output.
**Networking** (POSIX / DPDK / OSv stacks) + the production HTTP server + RPC framework are mostly irrelevant to
a batch assembler but exist if a service mode is ever wanted.

---

## 7. Inter-shard communication (where multicore actually comes from)

- **`seastar::smp::submit_to(shard, lambda)`** — run a lambda on a specific shard; `co_await` its result. The
  only way to touch another shard's data. Each round-trip adds latency — **design ownership so the owning shard
  does its own work** (this is the crux for the SdBG; see §0B/§11).
- **`seastar::sharded<T>`** — the service pattern: one `T` instance per shard.

```cpp
seastar::sharded<my_service> svc;
co_await svc.start(args...);              // construct T on every shard
co_await svc.invoke_on_all([](my_service& s){ return s.run(); });  // concurrent on all shards
co_await svc.stop();                      // calls T::stop() everywhere (T::stop() is mandatory)
```

`invoke_on(shard, fn)`, `local()`, `local_is_initialized()` round it out. **Multicore parallelism = partition
the work across shards and `invoke_on_all`** — there is no shared-memory parallel-for across cores.

---

## 8. Build

C++20 minimum (coroutines/concepts/ranges), C++23 fully supported (we target C++23 already). Clang 10+/GCC 10+,
CMake 3.16+.

```cmake
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 23)
find_package(Seastar REQUIRED)
target_link_libraries(my_app PRIVATE Seastar::seastar)
```

`./configure.py --mode={debug|dev|release|sanitize}` then `ninja -C build/<mode>`. Perf: release ≈ 2× dev,
150× sanitize, 300× debug. **On macOS: build/run inside a Linux container** (§0A) — `configure.py` won't target Darwin.

---

## 9. Pitfalls & rules (from real 50K+ LOC Seastar codebases)

**Correctness:** (1) never block the reactor; (2) lambda-coroutine in `.then()` → UAF (use `coroutine::lambda()`
or a named lvalue); (3) reference params to a coroutine dangle across `co_await` (pass by value / `do_with`);
(4) `shared_ptr` destructing on the wrong shard (use `foreign_ptr<>`); (5) `do_with` lambda missing `&` copies;
(6) fire-and-forget futures need a `gate`.
**Performance:** (7) CPU loops need periodic `co_await maybe_yield()`; (8) `.then()` chains do *not* auto-insert
preemption checks (only coroutines do); (9) every `smp::submit_to` round-trip is latency — keep data shard-local;
(10) pin `CMAKE_CXX_STANDARD` ≥20 explicitly.

---

## 10. Project structure & entry point

```
src/
  main.cc                 # app_template + sharded<> lifecycle
  service/                # sharded service classes (one instance/shard), each with stop()
  storage/                # async disk abstractions
```

```cpp
#include <seastar/core/app-template.hh>
#include <seastar/core/sharded.hh>
static seastar::sharded<my_service> svc;
int main(int argc, char** argv) {
    seastar::app_template app;
    return app.run(argc, argv, [&] () -> seastar::future<> {
        co_await svc.start();
        co_await svc.invoke_on_all(&my_service::initialize);
        co_await svc.stop();
    });
}
```
Every service class: a mandatory `seastar::future<> stop()` (closes its `gate_`), shard-local state only, no
cross-shard `shared_ptr`.

---

## 11. MEGAHIT conversion sketch (and what to prototype first)

**The pipeline as coroutines.** The Python driver's stage sequence (`build_library → count → read2sdbg/seq2sdbg
→ iterate → assemble → local → merge`) becomes `co_await`-ed coroutine stages in one Seastar app — directly
realizing the in-process pipeline win (no fork/exec, no inter-stage disk round-trips) from the prior architecture doc.

**The crux — sharding the SdBG.** Multicore in Seastar = `sharded<T>` + `invoke_on_all`, which demands the
work be partitionable with shard-local data. So the migration's first hard question is how to split the dBG so
each shard owns a partition and traversals stay shard-local (cross-shard navigation = `submit_to` latency on the
already-latency-bound 56% stage). The CX1 build's bucket structure (65536 buckets) is a natural partition axis to
explore; graph *traversal* locality is the risk.

**The CX1 bug becomes moot in the right way.** The arm64 `-t>1` corruption (root-caused: TOCTOU on shared offset
state in `base_engine.cpp Lv2Sort` + non-atomic `SaveSnapshot` RMW — see
[`coroutine-parallelism-architecture.md`](coroutine-parallelism-architecture.md)) is a *shared-mutable-state*
bug. Shared-nothing sharding removes the shared state by construction — but note the bug's fix (hoist offsets to
immutable values, single-owner shards) is the *same idea* Seastar enforces structurally.

**Prove-the-lever first (this repo's discipline):** before migrating the codebase, prototype and measure —
1. A minimal Seastar app (in a Linux container) that loads a real SdBG and runs the first-sweep traversal on one shard — establishes the Seastar baseline vs the current single-thread number.
2. A **sharded** SdBG-partition + first-sweep across N shards, measuring **cross-shard message rate** — the falsifiable hypothesis: *the dBG can be partitioned so <X% of traversal steps cross a shard*. If traversal is inherently shard-crossing, Seastar's model loses to shared-memory here, and that's a measured result to act on.
3. Decide the platform (§0A) — these benchmarks only mean something on the target you'll actually ship.

Keep every step gated bit-identical against `final.contigs.fa` md5, as with all prior work.
