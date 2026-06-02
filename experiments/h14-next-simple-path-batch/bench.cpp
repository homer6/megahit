// H14 — SDBG::NextSimplePathEdgeBatch: the actual composite the `assemble` first sweep filters on
// (`NextSimplePathEdge(e) == kNullID`, one call per edge over the whole graph). It's two-stage *dependent*:
// next = UniqueNextEdge(e); keep iff UniquePrevEdge(next) is also unique. Batched as two phases (batch stage 1
// over all edges, gather non-null successors, batch the dependent stage 2 over those, recombine) — composed
// from the validated H12 + H13 batches. Validate == scalar on the real graph, then measure. (C++17.)
#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "sdbg/sdbg.h"
static inline uint64_t xs(uint64_t& s){ s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static const char* kPrefix = "/Users/stevesperandeo/dev/homer6/megahit/profiling/prof_keeptmp/tmp/k21/21";

struct Data { SDBG g; std::vector<uint64_t> ids, out, ref; };
static Data& data() {
  static Data d;
  static bool init = [] {
    d.g.LoadFromFile(kPrefix);
    uint64_t N = d.g.size(); const long Q = 2'000'000; uint64_t s = 0xBE14ULL;
    d.ids.resize(Q); d.out.assign(Q, 0); d.ref.assign(Q, 0);
    for (long i = 0; i < Q; ++i) d.ids[i] = (xs(s) % (N - 1)) + 1;  // avoid 0 (Backward needs id-1)
    for (long i = 0; i < Q; ++i) d.ref[i] = d.g.NextSimplePathEdge(d.ids[i]);
    d.g.NextSimplePathEdgeBatch(d.ids.data(), d.out.data(), Q);
    long bad = 0; for (long i = 0; i < Q; ++i) bad += (d.out[i] != d.ref[i]);
    std::printf("[h14] SDBG size=%llu; Q=%ld; NextSimplePathEdgeBatch == scalar: %s\n",
                (unsigned long long)N, Q, bad ? "FAIL" : "PASS");
    return true;
  }(); (void)init; return d;
}
static void BM_scalar(benchmark::State& st){ auto&d=data(); for(auto _:st){ for(size_t i=0;i<d.ids.size();++i) d.out[i]=d.g.NextSimplePathEdge(d.ids[i]); benchmark::DoNotOptimize(d.out.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size()); }
static void BM_batch (benchmark::State& st){ auto&d=data(); for(auto _:st){ d.g.NextSimplePathEdgeBatch(d.ids.data(), d.out.data(), d.ids.size()); benchmark::DoNotOptimize(d.out.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size()); }
BENCHMARK(BM_scalar)->Name("NextSimplePathEdge/scalar")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_batch)->Name("NextSimplePathEdgeBatch")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
