// H15 — the foundation primitives the simplifier sweeps need: OutgoingEdgesBatch / IncomingEdgesBatch
// (kFlagWriteOut collect, for tip/low-depth/weak-link removal) and EdgeOutdegreeZeroBatch /
// EdgeIndegreeZeroBatch (kFlagMustEq0, for sdbg_pruning RemoveTips). Each = ForwardBatch/BackwardBatch +
// the *identical* scalar scan (same construction as UniqueNextEdgeBatch). Validate == scalar, then measure.
#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "sdbg/sdbg.h"
static inline uint64_t xs(uint64_t& s){ s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static const char* kPrefix = "/Users/stevesperandeo/dev/homer6/megahit/profiling/prof_keeptmp/tmp/k21/21";

struct Data { SDBG g; std::vector<uint64_t> ids, oe, ie; std::vector<int> od, id_; std::vector<char> ozb, izb; };
static Data& data() {
  static Data d;
  static bool init = [] {
    d.g.LoadFromFile(kPrefix);
    uint64_t N = d.g.size(); const long Q = 2'000'000; uint64_t s = 0x150FULL;
    d.ids.resize(Q);
    for (long i = 0; i < Q; ++i) d.ids[i] = (xs(s) % (N - 1)) + 1;
    d.oe.assign(Q*4, 0); d.ie.assign(Q*4, 0); d.od.assign(Q,0); d.id_.assign(Q,0);
    d.ozb.assign(Q,0); d.izb.assign(Q,0);
    // batch
    d.g.OutgoingEdgesBatch(d.ids.data(), d.od.data(), d.oe.data(), Q);
    d.g.IncomingEdgesBatch(d.ids.data(), d.id_.data(), d.ie.data(), Q);
    std::vector<bool> ozt(Q), izt(Q);  // can't take .data() on vector<bool>; use a local bool array
    { std::vector<char> tmp(Q); d.g.EdgeOutdegreeZeroBatch(d.ids.data(), reinterpret_cast<bool*>(tmp.data()), Q); d.ozb = tmp; }
    { std::vector<char> tmp(Q); d.g.EdgeIndegreeZeroBatch (d.ids.data(), reinterpret_cast<bool*>(tmp.data()), Q); d.izb = tmp; }
    // scalar reference + compare
    long bad_oe=0, bad_ie=0, bad_oz=0, bad_iz=0;
    uint64_t ref[4];
    for (long i = 0; i < Q; ++i) {
      int rd = d.g.OutgoingEdges(d.ids[i], ref);
      if (rd != d.od[i]) ++bad_oe;
      else for (int j=0;j<rd;++j) if (ref[j]!=d.oe[i*4+j]) { ++bad_oe; break; }
      int rid = d.g.IncomingEdges(d.ids[i], ref);
      if (rid != d.id_[i]) ++bad_ie;
      else for (int j=0;j<rid;++j) if (ref[j]!=d.ie[i*4+j]) { ++bad_ie; break; }
      if ((d.g.EdgeOutdegreeZero(d.ids[i]) ? 1 : 0) != (d.ozb[i] ? 1 : 0)) ++bad_oz;
      if ((d.g.EdgeIndegreeZero (d.ids[i]) ? 1 : 0) != (d.izb[i] ? 1 : 0)) ++bad_iz;
    }
    std::printf("[h15] N=%llu Q=%ld  OutgoingEdgesBatch:%s  IncomingEdgesBatch:%s  OutdegreeZeroBatch:%s  IndegreeZeroBatch:%s\n",
                (unsigned long long)N, Q, bad_oe?"FAIL":"PASS", bad_ie?"FAIL":"PASS", bad_oz?"FAIL":"PASS", bad_iz?"FAIL":"PASS");
    return true;
  }(); (void)init; return d;
}
static void BM_oe_scalar(benchmark::State& st){auto&d=data();uint64_t r[4];for(auto _:st){for(size_t i=0;i<d.ids.size();++i)d.od[i]=d.g.OutgoingEdges(d.ids[i],r);benchmark::DoNotOptimize(d.od.data());}st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size());}
static void BM_oe_batch (benchmark::State& st){auto&d=data();for(auto _:st){d.g.OutgoingEdgesBatch(d.ids.data(),d.od.data(),d.oe.data(),d.ids.size());benchmark::DoNotOptimize(d.oe.data());}st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size());}
static void BM_oz_scalar(benchmark::State& st){auto&d=data();for(auto _:st){for(size_t i=0;i<d.ids.size();++i)d.ozb[i]=d.g.EdgeOutdegreeZero(d.ids[i]);benchmark::DoNotOptimize(d.ozb.data());}st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size());}
static void BM_oz_batch (benchmark::State& st){auto&d=data();std::vector<char>t(d.ids.size());for(auto _:st){d.g.EdgeOutdegreeZeroBatch(d.ids.data(),reinterpret_cast<bool*>(t.data()),d.ids.size());benchmark::DoNotOptimize(t.data());}st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size());}
BENCHMARK(BM_oe_scalar)->Name("OutgoingEdges/scalar")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_oe_batch)->Name("OutgoingEdgesBatch")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_oz_scalar)->Name("EdgeOutdegreeZero/scalar")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_oz_batch)->Name("EdgeOutdegreeZeroBatch")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
