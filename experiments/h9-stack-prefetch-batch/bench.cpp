// H9 — the realistic batched API (C++26). The monolithic sorted batch (h8) regressed because the
// std::sort with an indirect comparator dominates. The allocation-free PREFETCH-AHEAD batch (no
// buffer, no sort, order-preserving) is the right shape for in-flight traversal. We also show the
// stack-buffer answer to "inplace_vector?" : std::inplace_vector isn't in libc++ yet, but a bounded
// batch needs no heap at all — prefetch uses zero scratch.
//
// build: STD=c++26 ../build.sh ; run: ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true
#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

struct Data {
  BitRS rs; std::vector<uint64_t> packed; uint64_t ones;
  std::vector<int64_t> k, p, out, ref; std::vector<uint32_t> scratch;
};
static Data& data() {
  static Data d = [] {
    Data x; const int64_t NB = 512LL << 20;
    x.ones = build_random_bitvector(x.rs, x.packed, NB, 0xABCDEFULL);
    const long Q = 2'000'000; uint64_t s = 0xB47C4EDULL;
    x.k.resize(Q); x.p.resize(Q); x.out.assign(Q, 0); x.ref.assign(Q, 0);
    for (long i = 0; i < Q; ++i) { x.k[i] = (int64_t)(xs(s) % x.ones); x.p[i] = (int64_t)(xs(s) % (uint64_t)(NB - 1)); }
    // correctness of the prefetch variants vs per-query
    long bad = 0;
    for (long i = 0; i < Q; ++i) x.ref[i] = x.rs.select(x.k[i]);
    x.rs.select_batch_prefetch(1, x.k.data(), x.out.data(), Q); for (long i=0;i<Q;++i) bad += (x.out[i]!=x.ref[i]);
    for (long i = 0; i < Q; ++i) x.ref[i] = x.rs.rank(x.p[i]);
    x.rs.rank_batch_prefetch(1, x.p.data(), x.out.data(), Q);    for (long i=0;i<Q;++i) bad += (x.out[i]!=x.ref[i]);
    std::printf("[h9] 512 Mbit, Q=%ld; prefetch-batch correctness: %s  (std::inplace_vector in libc++: NO -> std::array / no-buffer)\n",
                Q, bad ? "FAIL" : "PASS");
    return x;
  }();
  return d;
}
#define SINK(d) benchmark::DoNotOptimize((d).out.data())
static void BM_sel_pq    (benchmark::State& s){ auto&d=data(); for(auto _:s){ for(size_t i=0;i<d.k.size();++i) d.out[i]=d.rs.select(d.k[i]); SINK(d);} s.SetItemsProcessed(s.iterations()*(int64_t)d.k.size());}
static void BM_sel_sort  (benchmark::State& s){ auto&d=data(); for(auto _:s){ d.rs.select_batch(1,d.k.data(),d.out.data(),d.k.size(),&d.scratch); SINK(d);} s.SetItemsProcessed(s.iterations()*(int64_t)d.k.size());}
static void BM_sel_pref  (benchmark::State& s){ auto&d=data(); for(auto _:s){ d.rs.select_batch_prefetch(1,d.k.data(),d.out.data(),d.k.size()); SINK(d);} s.SetItemsProcessed(s.iterations()*(int64_t)d.k.size());}
static void BM_rank_pq   (benchmark::State& s){ auto&d=data(); for(auto _:s){ for(size_t i=0;i<d.p.size();++i) d.out[i]=d.rs.rank(d.p[i]); SINK(d);} s.SetItemsProcessed(s.iterations()*(int64_t)d.p.size());}
static void BM_rank_pref (benchmark::State& s){ auto&d=data(); for(auto _:s){ d.rs.rank_batch_prefetch(1,d.p.data(),d.out.data(),d.p.size()); SINK(d);} s.SetItemsProcessed(s.iterations()*(int64_t)d.p.size());}
BENCHMARK(BM_sel_pq)  ->Name("select/per-query")              ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_sel_sort)->Name("select/batch-sorted (heap+sort)")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_sel_pref)->Name("select/batch-prefetch (no alloc)")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_rank_pq)  ->Name("rank/per-query")               ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_rank_pref)->Name("rank/batch-prefetch (no alloc)")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
