// MEGAHIT Seastar service — the per-shard assembly engine.
//
// Phase 0 of the migration (docs/megahit-as-a-service.md §1). One AssemblyEngine instance per shard
// (`seastar::sharded<AssemblyEngine>`). It runs the legacy multi-k pipeline as `co_await`-ed stages in-process
// — replacing the Python driver's fork/exec + disk round-trips. Each stage currently calls the existing
// `megahit_core` entry point (`main_*`) inside a `seastar::thread`; later phases replace each with native typed
// code over shard-owned data. The CX1 `-t>1` corruption dissolves once the build stages move to shard-owned
// state (docs/coroutine-parallelism-architecture.md §4).
#pragma once

#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>

#include "job.hh"

namespace megahit {

class AssemblyEngine {
 public:
  // Run the full pipeline for one sample on this shard. Returns the final-contigs summary.
  seastar::future<AssemblyResult> run(SampleJob job);

  // Mandatory for seastar::sharded<>: drains in-flight work before the instance is destroyed.
  seastar::future<> stop();

 private:
  // Pipeline stages (docs/legacy-driver-spec.md §1–2). Each builds the megahit_core argv per the spec and runs
  // the stage off-reactor. Behavior must match the driver bit-for-bit (gate: final.contigs.fa md5).
  seastar::future<> build_library(const SampleJob& job);
  seastar::future<> build_first_graph(const SampleJob& job);                   // count|read2sdbg @ k_min
  seastar::future<> assemble(const SampleJob& job, int k);
  seastar::future<> local_assemble(const SampleJob& job, int k);
  seastar::future<> iterate(const SampleJob& job, int k_from, int k_to);
  seastar::future<> build_graph(const SampleJob& job, int k, int k_from);      // seq2sdbg
  seastar::future<AssemblyResult> merge_final(const SampleJob& job);

  seastar::gate gate_;  // tracks in-flight stage fibers so stop() can drain them
};

}  // namespace megahit
