# MEGAHIT as a scalable Linux Seastar assembly service

> **Status / provenance.** Architecture design for the Seastar conversion, grounded in the existing pipeline
> (`src/megahit` driver — now removed; its functional contract is preserved in
> [`legacy-driver-spec.md`](legacy-driver-spec.md)), the measured profile ([`workload-profile.md`](workload-profile.md)),
> and a research pass on distributed-assembly prior art (HipMer/MetaHipMer, Ray, ABySS). **Design, not built.**
> Every tier carries a falsifiable hypothesis + kill criterion. Companion: [`seastar-guide.md`](seastar-guide.md).

## Framing — two scaling models, kept separate

| | Model | Risk | Verdict |
|---|---|---|---|
| **Tier T** | **Throughput scale-out** — many independent sample assemblies fanned across nodes; each sample assembled wholly on one node's shards. | Embarrassingly parallel, near-linear. | **Build this — it's what the field actually needs.** |
| **Tier S** | **Single-assembly scale-out** — one (co-)assembly too big for a node; its dBG partitioned across nodes with cross-node messaging on traversal. | Research-grade (the HipMer problem); hits the 56%-latency-bound `assemble` stage. | **Gate behind a measurement; likely loses except for terabase co-assembly.** |

Ship T as the product; gate S behind the falsifiable locality hypothesis (§5).

## 1. Service model — single node first

**The pipeline as `co_await`-ed coroutine stages.** The driver's strict per-sample serial DAG
(`build_library → build_first_graph(k_min) → assemble(k_min) → ∀k: local_assemble → iterate → build_graph(seq2sdbg)
→ assemble → merge_final`; see [`legacy-driver-spec.md`](legacy-driver-spec.md)) becomes one coroutine per sample
on its owning shard, each stage a `co_await` instead of a fork/exec + disk round-trip:

```cpp
seastar::future<AssemblyResult> AssemblyEngine::run(SampleJob job) {
  auto lib  = co_await build_library(job);
  auto sdbg = co_await build_first_graph(lib, k_min);          // count | read2sdbg
  auto contigs = co_await assemble(sdbg, k_min);
  int kp = k_min;
  for (int k : job.k_list) {
    co_await local_assemble(contigs, k);
    auto edges = co_await iterate(contigs, kp, k);
    sdbg = co_await build_graph(edges, k);                     // seq2sdbg
    contigs = co_await assemble(sdbg, k);
    kp = k;
  }
  co_return co_await merge_final(contigs);
}
```

Free structural wins (already identified in [`coroutine-parallelism-architecture.md`](coroutine-parallelism-architecture.md)):
no fork/exec + no inter-stage disk serialization (SdBG/SeqPackage stay in the shard heap); stage pipelining
(`iterate(kᵢ)` overlaps the tail of `assemble(kᵢ)`). The OpenMP `parallel for` inside the build engines becomes
in-shard Seastar parallelism over the 65536 CX1 buckets — and **the `-t>1` CX1 corruption dissolves by
construction** (its root cause is lock-protected-write/unlocked-read TOCTOU on shared offset state; Seastar's
shard-owns-its-data model replaces locking with ownership, so the bug class disappears).

**Within-node sharding.** Tier T uses **job-level sharding**: each sample pinned whole to one shard
(`sharded<AssemblyEngine>`, `hash(sample_id) % smp::count`, `invoke_on(shard, …)`) — its SdBG/SeqPackage/temp
all on that shard, **zero cross-shard sharing** (so `foreign_ptr<>` barely needed, allocator discipline trivially
met). **Bucket-level sharding** (CX1 `bucket % smp::count`) is only relevant *inside* one big assembly (Tier S, §3)
and does not make traversal cheap.

**Job API (Seastar gives the front door; we build policy).** Control plane: built-in `httpd` (`POST /jobs`,
`GET /jobs/{id}[/contigs]`) — best default for Nextflow/Snakemake. Reads-in/contigs-out: `rpc::sink/source`
streaming with **built-in future backpressure** (assembly starts while reads arrive; no giant per-sample alloc).
Admission: a global `semaphore` (its FIFO waiters = the admission queue); `scheduling_group` isolates control
plane from bulk assembly; the I/O scheduler arbitrates NVMe. Lifecycle: `app_template` → `sharded<>.start()` →
serve → `gate.close()` drains in-flight → `sharded<>.stop()`. **We own:** job-id gen, result store, queue policy,
persistence, retries/timeouts/cancel. `src/` has zero existing Seastar usage — clean greenfield on the C++23 base.

## 2. Tier T — throughput scale-out (build this)

Single-node §1 lifted across nodes by routing the **job-id space, never the graph** — exactly the Scylla/Redpanda
layering: **RPC picks the node, `smp::submit_to` picks the core** (`job_id → (node, shard)`). Network cost is paid
**once per job** (submit + result), never on the hot traversal path; inside the node it is verbatim §1.

| Seastar provides | We build |
|---|---|
| `sharded<>`, `invoke_on`, `smp::submit_to`, `rpc` transport + `sink/source` backpressure, `httpd`, `semaphore`/`gate`/`scheduling_group` | job-id routing, cluster membership/discovery, result store, retries/timeouts/cancel, per-tenant quota/persistence |

**Near-linear** — samples are wholly independent (zero cross-sample state in the driver); bounded by NVMe/network
read ingest and the largest single sample fitting one shard's RAM share. **This is what routine metagenomics does**
(one single-node assembler per sample, fanned by Nextflow/Snakemake scatter-gather); distributed single-assembly is
the deliberate co-assembly exception. Tier T covers the vast majority of demand at near-zero algorithmic risk.

## 3. Tier S — single-assembly distributed dBG (hard, optional)

Justified **only** when one pooled co-assembly exceeds a node's RAM (terabase, low-abundance genome recovery). It
splits into a benign half and a brutal half.

**Benign half — build/iterate/local (MapReduce, distributes well).** Universal partition:
`owner(kmer) = hash(canonical(kmer)) mod P` — O(1), no global lookup. MEGAHIT's CX1 already content-addresses by
the canonical (k+1)-mer's 8-char prefix bucket → a ready-made ownership map (caveat: a *prefix* extract has
load-skew on low-complexity regions; a balanced build should hash the canonical k-mer or use a **minimizer**).
count/read2sdbg/seq2sdbg/iterate are map→shuffle→reduce: the shuffle is **one all-to-all, bandwidth-bound,
RPC-streamable**. local assembly (30%, compute-bound, per-contig-independent) distributes nearly as well as Tier T.
Two borrowed techniques are mandatory:
- **Supermers** — ship maximal minimizer-runs, not individual k-mers (~k-fold volume reduction; MetaHipMer measured
  5.3× volume / 3.7× message-count). The single most important Tier-S technique.
- **Aggregating stores (the ABySS trap)** — per-destination batched RPC, *never* one message per k-mer (ABySS 1.x's
  fine-grained MPI is ~64× slower than HipMer at 960 cores). Maps to `smp::submit_to` (cross-shard) / `rpc`
  (cross-node) with mandatory batching — the network analogue of the repo's batched-rank/select win.
- Plus a **high-frequency k-mer cap** + singleton Bloom prefilter (HipMer's k-mer analysis cut memory 6.93×) or one
  hyper-repetitive k-mer floods a single owner → straggler.

**Brutal half — distributed rank/select traversal in `assemble`.** This is MEGAHIT's profiled weak spot meeting the
network. The structural fact: edges are re-laid into one **global edge-ID space ordered by bucket**, and traversal
does **not** respect buckets — `Forward`/`Backward` (`sdbg.h:107-120`) `select()` to an *arbitrary global edge id*,
i.e. an arbitrary node. Why: the bucket key is the k-mer's first 8 chars, but adjacency *shifts the window by one
base*, so an edge in bucket `B(c1…c8)` has successors in `B(c2…c9)` — an unrelated bucket. **The partition is
perfect for building but not closed under graph adjacency** → a unitig walk crosses nodes at nearly every step.
And the walk is **strictly sequential-dependent** (hop N+1 needs hop N's result), so each remote hop is a full
network RTT on the critical path (~1–10 µs vs ~100 ns local DRAM miss = **10–100×**); `EdgeReverseComplement` is
O(k) dependent `Backward` calls. The repo's batched rank/select (2–3× locally) **cannot rescue this** — batching
helps *within* a node, but a single walk is sequential, so it can't hide cross-node latency. The cleaning loop
(`main_assemble.cpp:183-249`, 5 rounds each ending in a full-graph `Refresh`) becomes a **BSP global-barrier chain**
mutating shared `invalid_`/`id_map_` — cross-node delete agreement + a distributed id_map.

**The fundamental obstruction:** there is **no mature distributed *succinct* dBG** (BOSS/rank-select) — rank/select
is dependent pointer-chasing, latency-bound, bandwidth-irrelevant; splitting the bitvectors makes every cheap local
op a remote round-trip. What exists is distributed *construction* + *counting* only; traversal stays single-node. So
Tier-S MEGAHIT **cannot shard the SdBG and traverse across nodes** — it must keep the SdBG within-shard and adopt
the hash-partitioned **explicit-dBG + supermer** model everyone else uses, forfeiting the succinct locality at the
node boundary. HipMer's mitigations (all required): owner-as-oracle "continue the walk on the owner" RPC; thousands
of concurrent walks (coroutines suspend on RPC, others extend locally — overlap hides latency; `unitig_graph.cpp`
already iterates path-ends in parallel); lightweight/heavy contig work-stealing; coverage trimming of ultra-frequent
k-mers. Even so, MetaHipMer strong-scaling on one assembly is **sub-linear** (100→31→20 min at 8→32→64 nodes;
efficiency 80%→64%) — a *capacity* unlock, not a speedup.

## 5. The decisive question for Tier S (falsifiable)

> **H-S (locality):** there is a dBG partition across `P` nodes where the **cross-node-edge fraction `f_x`** (unitig
> steps whose successor is owned by a different node) stays below the break-even threshold for real metagenomes at
> production `k`, `P`.

Per-step cost ≈ `(1−f_x)·t_local + f_x·t_remote`; with walk-concurrency `C` hiding remote stalls, distributed beats
single-node only if `f_x < (P−1)·(t_local/t_remote)·C`. With `t_local/t_remote ≈ 1/50`: worst case `C≈1, P=8` →
**traversal must stay >86% node-local** (`f_x < 0.14`). **Kill criterion:** if, after the best (minimizer) partition,
measured `f_x > ~10%` on real data at production k with achievable `C`, Tier-S distributed assemble **loses** to
single-node shared memory — *and that is a publishable measured result, not an engineering failure; ship Tier T.*
**Prior art predicts `f_x` is high** (k-mer adjacency ≈ a random permutation of the ID space under hash/prefix
partitioning) — which is why minimizer partitioning (adjacent k-mers often share a minimizer → steps stay node-local)
is the only candidate, and why Tier T (which sidesteps the question entirely) is the product.

## 6. Phased plan (hypothesis + kill per phase)

- **Phase 0 — single-node Seastar service.** Port §1 to `sharded<AssemblyEngine>` + co_await stages + HTTP/RPC front
  door + semaphore admission + gate shutdown; OpenMP → in-shard Seastar parallelism (dissolves the `-t>1` CX1 bug by
  ownership). *H0:* output-correct and ≥ the CLI single-thread speed, multicore now correct. *Kill:* if re-modeling
  the shared `invalid_`/`id_map_` regresses single-node throughput vs the CLI, reconsider scope.
- **Phase 1 — multicore within a node** (job-level shards; per-sample on one shard, many samples across shards).
  *H1:* near-linear sample throughput across cores; bit-identical contigs (md5 vs the captured baseline `bf2c562…`).
- **Phase 2 — Tier T across nodes** (RPC job routing, result store, membership). *H2:* near-linear node scaling on a
  multi-sample workload; *Kill:* <0.7×N (network/ingest bound — fix ingest, not the graph).
- **Phase 3 — (optional) Tier S prototype**: minimizer-partitioned distributed *build* + *local* first (the benign
  half), then measure **`f_x`** on a real co-assembly before writing one line of distributed traversal. *Kill:* H-S
  fails (§5) → stop, ship Tier T, publish the `f_x` result.

Every phase gated bit-identical against `final.contigs.fa` md5, as with all prior work.
