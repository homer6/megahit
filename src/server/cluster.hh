// MEGAHIT Seastar service — the cluster layer (cluster-first).
//
// MEGAHIT is a cluster of homogeneous Seastar nodes. A SampleJob is owned by exactly one node
// (`hash(job_id) % node_count`); the owning node assembles the whole sample on its shards
// (`sharded<AssemblyEngine>`). A job submitted to any node is routed to its owner — run locally if we are the
// owner, else forwarded over Seastar RPC — and the AssemblyResult comes back the same way. A 1-node deployment
// is just a cluster of one: there is no single-node-only code path. (Tier T, docs/megahit-as-a-service.md §2.)
// Membership is a static peer list for now; dynamic discovery is a later phase.
#pragma once

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sstring.hh>
#include <seastar/net/socket_defs.hh>

#include <cstdint>
#include <memory>
#include <vector>

#include "assembly_engine.hh"
#include "job.hh"

namespace seastar::rpc { class protocol_base; }

namespace megahit {

// Every node holds the full peer list (index == node id) and knows its own id.
struct ClusterConfig {
  unsigned node_id = 0;
  std::vector<seastar::socket_address> peers;  // all nodes incl. self; peers[node_id] is this node's addr
};

class Cluster {
 public:
  Cluster(ClusterConfig cfg, seastar::sharded<AssemblyEngine>& engine);
  ~Cluster();

  // Bring up the RPC server (register the submit verb) and lazily-connectable clients to peers.
  seastar::future<> start();
  seastar::future<> stop();

  // Submit a job from anywhere in the cluster. Routes to the owning node and returns its result.
  seastar::future<AssemblyResult> submit(SampleJob job);

  unsigned node_count() const { return static_cast<unsigned>(cfg_.peers.size()); }
  unsigned this_node() const { return cfg_.node_id; }

 private:
  unsigned owner(const seastar::sstring& job_id) const;
  bool is_local(unsigned node) const { return node == cfg_.node_id; }

  // Owner == this node: place the job on the right shard and run it there.
  seastar::future<AssemblyResult> run_local(SampleJob job);

  ClusterConfig cfg_;
  seastar::sharded<AssemblyEngine>& engine_;

  // RPC transport (server + per-peer clients). Defined in cluster.cc to keep the heavy seastar/rpc includes
  // out of the header; pimpl so callers don't pull in the serializer.
  struct Rpc;
  std::unique_ptr<Rpc> rpc_;
};

}  // namespace megahit
