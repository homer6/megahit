// MEGAHIT Seastar service — cluster layer implementation (Phase 0, cluster-first).
//
// Routing + local dispatch are real; the RPC transport is scaffolded (the per-field serializer for
// SampleJob/AssemblyResult is the one TODO — mechanical but verbose, completed against the Linux build).
#include "cluster.hh"

#include <seastar/core/coroutine.hh>
#include <seastar/core/smp.hh>
#include <seastar/rpc/rpc.hh>
#include <seastar/util/log.hh>

#include <functional>

namespace megahit {

namespace {
seastar::logger clog("cluster");

// RPC verb ids.
enum class verb : uint32_t { submit_job = 1 };

// --- RPC serializer -------------------------------------------------------------------------------------
// Seastar RPC needs read/write for every wire type. TODO(phase0-linux): implement field-by-field marshalling
// of SampleJob (ReadLibrary string vectors + the scalar options) and AssemblyResult. Mechanical; deferred
// until there's a compiler. Scalars/sstring/vector helpers go here; the per-struct read/write follow.
struct serializer {};
}  // namespace

// Pimpl: holds the RPC protocol, the server, and lazily-created clients to peers.
struct Cluster::Rpc {
  seastar::rpc::protocol<serializer> proto{serializer{}};
  std::unique_ptr<seastar::rpc::protocol<serializer>::server> server;
  std::vector<std::unique_ptr<seastar::rpc::protocol<serializer>::client>> clients;  // index = node id
};

Cluster::Cluster(ClusterConfig cfg, seastar::sharded<AssemblyEngine>& engine)
    : cfg_(std::move(cfg)), engine_(engine), rpc_(std::make_unique<Rpc>()) {}

Cluster::~Cluster() = default;

unsigned Cluster::owner(const seastar::sstring& job_id) const {
  return static_cast<unsigned>(std::hash<std::string_view>{}(
             std::string_view(job_id.data(), job_id.size())) %
         cfg_.peers.size());
}

seastar::future<> Cluster::start() {
  // Register the handler the OWNER runs when a peer forwards a job to us: run it locally, return the result.
  rpc_->proto.register_handler(
      static_cast<uint32_t>(verb::submit_job),
      [this](SampleJob job) -> seastar::future<AssemblyResult> { return run_local(std::move(job)); });

  // Server listens for forwarded jobs on this node's address.
  seastar::rpc::server_options so;
  rpc_->server = std::make_unique<seastar::rpc::protocol<serializer>::server>(
      rpc_->proto, cfg_.peers[cfg_.node_id], so);

  // One client per peer (self slot stays null — local jobs never go over the wire).
  rpc_->clients.resize(cfg_.peers.size());
  for (unsigned n = 0; n < cfg_.peers.size(); ++n) {
    if (n == cfg_.node_id) continue;
    rpc_->clients[n] = std::make_unique<seastar::rpc::protocol<serializer>::client>(
        rpc_->proto, cfg_.peers[n]);
  }
  clog.info("cluster node {} listening on {} ({} node(s))", cfg_.node_id,
            cfg_.peers[cfg_.node_id], cfg_.peers.size());
  return seastar::make_ready_future<>();
}

seastar::future<> Cluster::stop() {
  for (auto& c : rpc_->clients) {
    if (c) co_await c->stop();
  }
  if (rpc_->server) co_await rpc_->server->stop();
}

seastar::future<AssemblyResult> Cluster::run_local(SampleJob job) {
  // We own this job: pick the shard within this node and run the full pipeline there.
  const unsigned shard = owner(job.id) % seastar::smp::count;  // intra-node shard placement
  return engine_.invoke_on(shard, [job = std::move(job)](AssemblyEngine& e) mutable {
    return e.run(std::move(job));
  });
}

seastar::future<AssemblyResult> Cluster::submit(SampleJob job) {
  const unsigned node = owner(job.id);
  if (is_local(node)) {
    co_return co_await run_local(std::move(job));
  }
  // Forward to the owning node and await its result.
  clog.debug("routing job {} to node {}", job.id, node);
  auto handler = rpc_->proto.make_client<seastar::future<AssemblyResult>(SampleJob)>(
      static_cast<uint32_t>(verb::submit_job));
  co_return co_await handler(*rpc_->clients[node], std::move(job));
}

}  // namespace megahit
