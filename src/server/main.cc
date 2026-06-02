// MEGAHIT Seastar service — cluster node entry point (Phase 0, cluster-first).
//
// Every process is a cluster node: it brings up `sharded<AssemblyEngine>` (one engine per core) and the
// Cluster layer (RPC + job routing), then serves. A job submitted anywhere is routed to its owning node
// (docs/megahit-as-a-service.md §2). A 1-node deployment is a degenerate cluster — same code path. Build/run
// on Linux (Seastar is Linux-only; docs/seastar-guide.md §0).
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/smp.hh>
#include <seastar/net/inet_address.hh>
#include <seastar/net/socket_defs.hh>
#include <seastar/util/log.hh>

#include <boost/program_options.hpp>

#include <string>
#include <vector>

#include "assembly_engine.hh"
#include "cluster.hh"
#include "job.hh"

namespace bpo = boost::program_options;

namespace {
seastar::logger slog("megahit-server");

// Parse a comma-separated "host:port,host:port,..." peer list (index == node id) into socket addresses.
std::vector<seastar::socket_address> parse_peers(const std::string& csv) {
  std::vector<seastar::socket_address> peers;
  std::size_t pos = 0;
  while (pos < csv.size()) {
    auto comma = csv.find(',', pos);
    auto tok = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if (!tok.empty()) peers.emplace_back(seastar::ipv4_addr(seastar::sstring(tok)));
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return peers;
}

megahit::SampleJob job_from_opts(const bpo::variables_map& cfg) {
  megahit::SampleJob job;
  job.id = "cli-0";
  job.out_dir = cfg["out"].as<std::string>();
  if (cfg.count("pe1")) job.lib.pe1.push_back(cfg["pe1"].as<std::string>());
  if (cfg.count("pe2")) job.lib.pe2.push_back(cfg["pe2"].as<std::string>());
  job.k_list = {21, 29, 39, 59, 79, 99};  // k-list resolution belongs in the control plane (spec §3)
  job.num_cpu_threads = 1;
  return job;
}
}  // namespace

int main(int argc, char** argv) {
  seastar::app_template app;
  app.add_options()
      ("node-id", bpo::value<unsigned>()->default_value(0), "this node's id within the cluster")
      ("peers", bpo::value<std::string>()->default_value("127.0.0.1:7000"),
       "comma-separated host:port of ALL cluster nodes (list index = node id)")
      ("out", bpo::value<std::string>()->default_value("megahit_out"), "output directory")
      ("pe1", bpo::value<std::string>(), "paired-end reads R1 (CLI job)")
      ("pe2", bpo::value<std::string>(), "paired-end reads R2 (CLI job)")
      ("run-cli-job", bpo::bool_switch(), "submit one CLI job to the cluster then exit (Phase 0)");

  return app.run(argc, argv, [&app]() -> seastar::future<int> {
    auto& cfg = app.configuration();
    megahit::ClusterConfig cc;
    cc.node_id = cfg["node-id"].as<unsigned>();
    cc.peers = parse_peers(cfg["peers"].as<std::string>());

    static seastar::sharded<megahit::AssemblyEngine> engine;
    co_await engine.start();
    megahit::Cluster cluster(std::move(cc), engine);
    co_await cluster.start();
    slog.info("megahit node {} up: {} shard(s), {} cluster node(s)", cluster.this_node(),
              seastar::smp::count, cluster.node_count());

    int rc = 0;
    if (cfg["run-cli-job"].as<bool>()) {
      try {
        auto result = co_await cluster.submit(job_from_opts(cfg));  // routed by the cluster
        slog.info("done: {} ({} contigs, {} bp, N50 {})", result.contigs_path,
                  result.num_contigs, result.total_bp, result.n50);
      } catch (const std::exception& e) {
        slog.error("assembly failed: {}", e.what());
        rc = 1;
      }
    } else {
      // TODO(phase0): serve until signal once the HTTP/RPC job front door is wired (the RPC server is already
      // up, so peer-forwarded jobs are served). For now, bring up and exit.
      slog.info("node serving (job front door not yet implemented — Phase 0)");
    }

    co_await cluster.stop();
    co_await engine.stop();
    co_return rc;
  });
}
