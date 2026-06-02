// MEGAHIT Seastar service — per-shard assembly engine (Phase 0 / strangler).
//
// The multi-k pipeline (docs/legacy-driver-spec.md §1) as `co_await`-ed coroutine stages, in one process, no
// fork/exec, no inter-stage disk round-trips. Each stage builds the megahit_core argv per the port spec and
// runs the EXISTING `main_*` entry point inside a `seastar::thread` (blocking + OpenMP work must never run on
// the reactor). This is the transitional step; later phases replace each stage with a native typed coroutine
// over shard-owned data. Flag fidelity is the contract from docs/legacy-driver-spec.md §2 — the eventual
// bit-identical gate (final.contigs.fa md5) is the check, runnable once the Linux build is up.
#include "assembly_engine.hh"

#include <seastar/core/coroutine.hh>
#include <seastar/core/thread.hh>      // seastar::async
#include <seastar/coroutine/maybe_yield.hh>
#include <seastar/util/log.hh>

#include <stdexcept>
#include <string>
#include <vector>

// Existing megahit_core stage entry points (src/main_*.cpp), called in-process. See src/main.cpp.
extern int main_build_lib(int argc, char** argv);
extern int main_kmer_count(int argc, char** argv);
extern int main_read2sdbg(int argc, char** argv);
extern int main_seq2sdbg(int argc, char** argv);
extern int main_assemble(int argc, char** argv);
extern int main_iterate(int argc, char** argv);
extern int main_local(int argc, char** argv);

namespace megahit {

namespace {
seastar::logger elog("assembly_engine");
using args_t = std::vector<std::string>;

// Path helpers — must match the driver's layout (docs/legacy-driver-spec.md §6: graph_prefix/contig_prefix).
std::string graph_prefix(const SampleJob& j, int k) {
  return std::string(j.out_dir) + "/tmp/k" + std::to_string(k) + "/" + std::to_string(k);
}
std::string contig_prefix(const SampleJob& j, int k) {
  return std::string(j.out_dir) + "/intermediate_contigs/k" + std::to_string(k);
}

// Run a legacy stage off the reactor (seastar::async => a seastar::thread where blocking is allowed). Builds a
// throwaway argv; argv[0] = stage name. Throws on non-zero rc so the coroutine sees an exceptional future.
seastar::future<> run_stage(int (*entry)(int, char**), std::string name, args_t args) {
  return seastar::async([entry, name = std::move(name), args = std::move(args)]() mutable {
    elog.info("stage {} starting", name);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(name.data());
    for (auto& a : args) argv.push_back(a.data());
    int rc = entry(static_cast<int>(argv.size()), argv.data());
    if (rc != 0) {
      throw std::runtime_error("stage '" + name + "' failed (rc=" + std::to_string(rc) + ")");
    }
    elog.info("stage {} done", name);
  });
}
}  // namespace

seastar::future<> AssemblyEngine::build_library(const SampleJob& job) {
  // megahit_core buildlib <reads.lib> <reads.lib>  — converts the read libs to the binary library.
  // (The reads.lib descriptor is written by the control plane; docs/legacy-driver-spec.md §7.)
  std::string lib = std::string(job.out_dir) + "/reads.lib";
  return run_stage(main_build_lib, "buildlib", {lib, lib});
}

seastar::future<> AssemblyEngine::build_first_graph(const SampleJob& job) {
  // k_min graph: 2-pass `count` (default) or 1-pass `read2sdbg` (--kmin-1pass). Flags per spec §2.
  const int k = job.k_min();
  args_t a = {"-k", std::to_string(k), "-m", std::to_string(job.min_count),
              "--host_mem", std::to_string(job.host_mem), "--mem_flag", "1",
              "--output_prefix", graph_prefix(job, k),
              "--num_cpu_threads", std::to_string(job.num_cpu_threads),
              "--read_lib_file", std::string(job.out_dir) + "/reads.lib"};
  if (!job.no_mercy) a.push_back("--need_mercy");
  return job.kmin_1pass ? run_stage(main_read2sdbg, "read2sdbg", std::move(a))
                        : run_stage(main_kmer_count, "count", std::move(a));
}

seastar::future<> AssemblyEngine::build_graph(const SampleJob& job, int k, int k_from) {
  // seq2sdbg: build the k-graph from prior contigs + iterative edges. Conditional --input_prefix/--addi_contig/
  // --local_contig/--contig/--bubble per spec §2 (presence-of-file gated; omitted here — control plane stages
  // the inputs). Transitional: exact conditionals to be wired with the bit-identical gate.
  args_t a = {"-k", std::to_string(k), "--host_mem", std::to_string(job.host_mem), "--mem_flag", "1",
              "--output_prefix", graph_prefix(job, k),
              "--num_cpu_threads", std::to_string(job.num_cpu_threads),
              "--input_prefix", graph_prefix(job, k),
              "--contig", contig_prefix(job, k_from) + ".contigs.fa"};
  return run_stage(main_seq2sdbg, "seq2sdbg", std::move(a));
}

seastar::future<> AssemblyEngine::assemble(const SampleJob& job, int k) {
  // Flags verified against the driver (docs/legacy-driver-spec.md §2). min_standalone = max(min(k_max*3-1,
  // 1.5*min_contig_len), min_contig_len) when max_tip_len<0.
  int kmax = job.k_max();
  int min_standalone =
      std::max(std::min(kmax * 3 - 1, static_cast<int>(job.min_contig_len * 1.5)), job.min_contig_len);
  args_t a = {"-s", graph_prefix(job, k), "-o", contig_prefix(job, k),
              "-t", std::to_string(job.num_cpu_threads),
              "--min_standalone", std::to_string(min_standalone),
              "--prune_level", std::to_string(job.prune_level),
              "--merge_len", std::to_string(job.merge_len),
              "--merge_similar", std::to_string(job.merge_similar),
              "--cleaning_rounds", std::to_string(job.cleaning_rounds),
              "--disconnect_ratio", std::to_string(job.disconnect_ratio),
              "--low_local_ratio", std::to_string(job.low_local_ratio),
              "--min_depth", std::to_string(job.prune_depth),
              "--bubble_level", std::to_string(job.bubble_level),
              "--max_tip_len", std::to_string(job.max_tip_len)};
  if (k < kmax) a.push_back("--careful_bubble");
  if (k == kmax) a.push_back("--is_final_round");
  if (job.no_local) a.push_back("--output_standalone");
  return run_stage(main_assemble, "assemble", std::move(a));
}

seastar::future<> AssemblyEngine::local_assemble(const SampleJob& job, int k) {
  args_t a = {"-c", contig_prefix(job, k) + ".contigs.fa",
              "-l", std::string(job.out_dir) + "/reads.lib",
              "-t", std::to_string(job.num_cpu_threads),
              "-o", contig_prefix(job, k) + ".local.fa"};
  return run_stage(main_local, "local", std::move(a));
}

seastar::future<> AssemblyEngine::iterate(const SampleJob& job, int k_from, int k_to) {
  args_t a = {"-c", contig_prefix(job, k_from) + ".contigs.fa",
              "-t", std::to_string(job.num_cpu_threads),
              "-k", std::to_string(k_from), "-s", std::to_string(k_to - k_from),
              "-o", graph_prefix(job, k_to),
              "-r", std::string(job.out_dir) + "/reads.lib"};
  return run_stage(main_iterate, "iterate", std::move(a));
}

seastar::future<AssemblyResult> AssemblyEngine::merge_final(const SampleJob& job) {
  // Final: concat/filter per-k contigs into <out>/final.contigs.fa + compute stats. The driver does this in
  // Python (filterbylen + concatenation); to be a native step. Phase 0: report the path; stats are filled by
  // the (to-be-ported) merge logic.
  AssemblyResult r;
  r.contigs_path = job.out_dir + "/final.contigs.fa";
  co_return r;
}

seastar::future<AssemblyResult> AssemblyEngine::run(SampleJob job) {
  // Hold the gate for the whole pipeline so stop() drains it. Pipeline order per docs/legacy-driver-spec.md §1.
  auto holder = gate_.hold();
  elog.info("assembly {} starting on shard (k-list size {})", job.id, job.k_list.size());

  co_await build_library(job);
  co_await build_first_graph(job);
  co_await assemble(job, job.k_min());

  int k_prev = job.k_min();
  for (std::size_t i = 1; i < job.k_list.size(); ++i) {
    int k = job.k_list[i];
    if (!job.no_local) co_await local_assemble(job, k_prev);
    co_await iterate(job, k_prev, k);
    co_await build_graph(job, k, k_prev);
    co_await assemble(job, k);
    k_prev = k;
    co_await seastar::coroutine::maybe_yield();  // be a good reactor citizen between k-iterations
  }

  auto result = co_await merge_final(job);
  elog.info("assembly {} done -> {}", job.id, result.contigs_path);
  co_return result;
}

seastar::future<> AssemblyEngine::stop() {
  elog.info("assembly engine stopping (draining in-flight)");
  return gate_.close();
}

}  // namespace megahit
