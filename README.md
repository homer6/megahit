MEGAHIT — distributed metagenome assembly server
=================================================

A hard fork of [voutcn/megahit](https://github.com/voutcn/megahit), being re-architected from a single-node
command-line assembler into a **Linux, multi-node, horizontally-scalable assembly service** built on
[Seastar](https://github.com/scylladb/seastar) — ScyllaDB's shard-per-core C++ framework (shared-nothing,
C++20 coroutines, the runtime behind Scylla and Redpanda).

MEGAHIT is an ultra-fast, memory-efficient de novo NGS assembler for metagenomes, built on an iterative
multi-*k* **succinct de Bruijn graph (SdBG)**. This fork keeps that assembly core and rebuilds everything
around it — orchestration, parallelism, and I/O — as a Seastar service that scales across cores and nodes.

Status
------

**Early and under active re-architecture.** The assembler core works and the build is C++23; the Seastar
service and multi-node scale-out are the in-progress target, not a delivered result. Direction:

- **Platform: Linux.** Seastar is Linux-only (epoll/`io_uring`, hugepages, optional DPDK) — develop on any host
  via a Linux container. *This fork began as a macOS / Apple Silicon optimization effort; that produced durable
  algorithmic findings (below), but the shipping target is now Linux.*
- **No OpenMP, no Python driver.** Replaced by a Seastar **shard-per-core, shared-nothing** runtime with a
  **C++20-coroutine** pipeline — each assembly stage (`build_library → count → build_graph → assemble → local
  → merge`) becomes a `co_await`-ed coroutine on a `sharded<>` service, in one process, no inter-stage disk
  round-trips.
- **Scales two ways** (see [`docs/seastar-guide.md`](docs/seastar-guide.md)):
  - **Throughput** — many independent assembly jobs (samples) distributed across nodes; near-linear, the
    common metagenomics need.
  - **One huge assembly** — a single SdBG partitioned across nodes (distributed de Bruijn graph); the
    ambitious, research-grade tier, gated on keeping graph traversal node-local.

Durable findings from the optimization work (algorithmic; absolute numbers to be re-measured on Linux):

- `assemble` (~56% of runtime) is succinct-dBG **rank/select**-bound. The **only** proven speedup lever is the
  **batched memory access pattern** (sort + software-prefetch the scattered queries, **3.07×**); the kernel,
  bit-vector layout, and NEON-popcount levers were **disproven by measurement**. See
  [`experiments/`](experiments/) (one falsifiable hypothesis per dir) and [`profiling-history/`](profiling-history/).
- The multithreaded **CX1-sort corruption** bug is **root-caused** (a shared-mutable-state race in the parallel
  sort) — and Seastar's shared-nothing model removes that bug class by construction.

Design + research notes live in [`docs/`](docs/) (start with [`docs/README.md`](docs/README.md)).

Build
-----

Target: **Linux + Seastar**, C++23 (Clang 16+ / GCC 11+, CMake 3.16+). On macOS/Windows, build inside a Linux
container.

```sh
git submodule update --init --recursive       # vendored deps (Seastar, etc.)
./configure.py --mode=release                  # Seastar: debug | dev | release | sanitize
ninja -C build/release
```

> The Seastar service entry point is under construction; during the transition the original assembler still
> builds via the legacy CMake path (`mkdir build && cd build && cmake .. && make -j`) and assembles a single
> sample. See [`docs/`](docs/) for the current state of the migration.

Usage
-----

The service interface (job submission / result retrieval over RPC) is being built. The single-sample assembly
the core performs:

```sh
megahit -1 pe_1.fq.gz -2 pe_2.fq.gz -o out     # contigs → out/final.contigs.fa
```

Upstream & credit
-----------------

Forked from **MEGAHIT** by Dinghua Li et al. (The University of Hong Kong & L3 Bioinformatics):
<https://github.com/voutcn/megahit>. For the upstream project's packaged releases (BioConda, Docker, prebuilt
Linux binaries), use the original repository.

- Li, D., Liu, C-M., Luo, R., Sadakane, K., and Lam, T-W. (2015) *MEGAHIT: An ultra-fast single-node solution
  for large and complex metagenomics assembly via succinct de Bruijn graph.* Bioinformatics.
  doi:[10.1093/bioinformatics/btv033](https://doi.org/10.1093/bioinformatics/btv033) — PMID 25609793.

License
-------

GPLv3, inherited from upstream MEGAHIT — see [LICENSE](LICENSE). This is a modified fork.
