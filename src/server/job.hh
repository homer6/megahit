// MEGAHIT Seastar service — job & result types.
//
// Phase 0 of the Seastar migration (see docs/megahit-as-a-service.md). A SampleJob is one assembly request;
// the AssemblyEngine (one instance per shard) runs the multi-k pipeline for it. Option fields mirror the
// legacy Python driver's Options (docs/legacy-driver-spec.md) — the contract the orchestrator must replicate.
#pragma once

#include <seastar/core/sstring.hh>

#include <cstdint>
#include <vector>

namespace megahit {

// One input read library (paired-end and/or single-end file lists), matching the driver's -1/-2/-r model.
struct ReadLibrary {
  std::vector<seastar::sstring> pe1;  // -1
  std::vector<seastar::sstring> pe2;  // -2
  std::vector<seastar::sstring> se;   // -r
};

// One assembly request. The k-list is already resolved + validated (odd, 15..kMaxK, adjacent step <=28) per
// docs/legacy-driver-spec.md §3; resolution lives in the control plane, not the engine.
struct SampleJob {
  seastar::sstring id;        // job id (also picks the owning shard: hash(id) % smp::count)
  ReadLibrary lib;
  seastar::sstring out_dir;   // <out>/ ; intermediates in <out>/intermediate_contigs/, result final.contigs.fa
  std::vector<int> k_list;    // resolved multi-k schedule, e.g. {21,29,39,59,79,99}

  // Assembly options (driver defaults; docs/legacy-driver-spec.md §4). Only the load-bearing ones for Phase 0.
  int min_count = 2;
  int min_contig_len = 200;
  int prune_level = 2;
  double prune_depth = 2;
  int merge_len = 20;
  double merge_similar = 0.95;
  int cleaning_rounds = 5;
  double disconnect_ratio = 0.1;
  double low_local_ratio = 0.2;
  int bubble_level = 2;
  int max_tip_len = -1;       // -1 => auto (k*2) per the driver
  bool no_local = false;
  bool no_mercy = false;
  bool kmin_1pass = false;

  // Resource budget for this job's stages (per-shard share of the node memory; threads within the stage).
  std::int64_t host_mem = 0;  // bytes; 0 => auto-detect at submit time
  int num_cpu_threads = 1;    // intra-stage parallelism (transitional; becomes shard-driven)

  int k_min() const { return k_list.empty() ? 21 : k_list.front(); }
  int k_max() const { return k_list.empty() ? 99 : k_list.back(); }
};

struct AssemblyResult {
  seastar::sstring contigs_path;  // <out_dir>/final.contigs.fa
  std::uint64_t num_contigs = 0;
  std::uint64_t total_bp = 0;
  std::uint32_t n50 = 0;
  std::uint32_t longest = 0;
};

}  // namespace megahit
