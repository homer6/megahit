// MEGAHIT Seastar service — entry point (Phase 0).
//
// Brings up `seastar::sharded<AssemblyEngine>` (one engine per core) and a clean start/stop lifecycle. For now
// it can run a single sample given on the command line (a smoke path to exercise the in-process coroutine
// pipeline); the job-submission front door (HTTP/RPC) + cross-node Tier-T routing land in later phases
// (docs/megahit-as-a-service.md §1.3 / §2). Build/run on Linux (Seastar is Linux-only; docs/seastar-guide.md §0).
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/smp.hh>
#include <seastar/util/log.hh>

#include <boost/program_options.hpp>

#include "assembly_engine.hh"
#include "job.hh"

namespace bpo = boost::program_options;

namespace {
seastar::logger slog("megahit-server");

// Build a SampleJob from Phase-0 command-line options (placeholder for the real job API). k-list resolution
// (odd, range, step<=28; docs/legacy-driver-spec.md §3) belongs in the control plane — hard-coded default here.
megahit::SampleJob job_from_opts(const bpo::variables_map& cfg) {
  megahit::SampleJob job;
  job.id = "cli-0";
  job.out_dir = cfg["out"].as<std::string>();
  if (cfg.count("pe1")) job.lib.pe1.push_back(cfg["pe1"].as<std::string>());
  if (cfg.count("pe2")) job.lib.pe2.push_back(cfg["pe2"].as<std::string>());
  job.k_list = {21, 29, 39, 59, 79, 99};
  job.num_cpu_threads = 1;  // transitional; becomes shard-driven
  return job;
}
}  // namespace

int main(int argc, char** argv) {
  seastar::app_template app;
  app.add_options()
      ("out", bpo::value<std::string>()->default_value("megahit_out"), "output directory")
      ("pe1", bpo::value<std::string>(), "paired-end reads R1 (smoke path)")
      ("pe2", bpo::value<std::string>(), "paired-end reads R2 (smoke path)")
      ("run-cli-job", bpo::bool_switch(), "run the single CLI job then exit (Phase 0 smoke)");

  return app.run(argc, argv, [&app]() -> seastar::future<int> {
    auto& cfg = app.configuration();
    static seastar::sharded<megahit::AssemblyEngine> engine;

    co_await engine.start();
    slog.info("megahit-server up on {} shard(s)", seastar::smp::count);

    int rc = 0;
    if (cfg["run-cli-job"].as<bool>()) {
      auto job = job_from_opts(cfg);
      const unsigned shard = std::hash<std::string>{}(std::string(job.id)) % seastar::smp::count;
      try {
        auto result = co_await engine.invoke_on(
            shard, [job = std::move(job)](megahit::AssemblyEngine& e) mutable {
              return e.run(std::move(job));
            });
        slog.info("done: {} ({} contigs, {} bp, N50 {})", result.contigs_path,
                  result.num_contigs, result.total_bp, result.n50);
      } catch (const std::exception& e) {
        slog.error("assembly failed: {}", e.what());
        rc = 1;
      }
    } else {
      slog.info("no --run-cli-job given; service front door (HTTP/RPC) not yet implemented (Phase 0)");
    }

    co_await engine.stop();
    co_return rc;
  });
}
