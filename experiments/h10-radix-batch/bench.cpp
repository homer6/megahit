// H10 — can a faster sort recover h3's 3x for batched select? The pair-sort (h9) is 1.43x because
// std::sort is ~half the batch time. Try an LSD radix sort (O(n), cache-friendly) of (key,index)
// pairs; same sorted-resolve. Compare: per-query / std::sort-pairs / radix.
//
// build: STD=c++26 ../build.sh ; run: ./bench --benchmark_min_time=0.5s --benchmark_repetitions=6 --benchmark_report_aggregates_only=true --benchmark_format=csv
#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;
using Pair = BitRS::BatchPair;   // {key=int64, idx=uint32}

// LSD radix sort of pairs by key, 11-bit digits, early-out past the max key's top bit.
static void radix_sort(std::vector<Pair>& a, std::vector<Pair>& tmp) {
  const int R = 11, B = 1 << R, M = B - 1; const size_t n = a.size(); tmp.resize(n);
  uint64_t maxk = 0; for (const auto& p : a) maxk = std::max<uint64_t>(maxk, (uint64_t)p.first);
  for (int shift = 0; (maxk >> shift) > 0; shift += R) {
    size_t cnt[B + 1]; for (int i = 0; i <= B; ++i) cnt[i] = 0;
    for (const auto& p : a) ++cnt[((uint64_t)p.first >> shift) & M];
    size_t s = 0; for (int i = 0; i < B; ++i) { size_t c = cnt[i]; cnt[i] = s; s += c; }
    for (const auto& p : a) tmp[cnt[((uint64_t)p.first >> shift) & M]++] = p;
    a.swap(tmp);
  }
}

struct Data { BitRS rs; std::vector<uint64_t> packed; uint64_t ones; std::vector<int64_t> k, out;
              std::vector<Pair> kv, tmp; };
static Data& data() {
  static Data d = [] { Data x; x.ones = build_random_bitvector(x.rs, x.packed, 512LL << 20, 0xABCDEFULL);
    const long Q = 2'000'000; uint64_t s = 0xB47C4EDULL; x.k.resize(Q); x.out.assign(Q, 0); x.kv.resize(Q);
    for (long i = 0; i < Q; ++i) x.k[i] = (int64_t)(xs(s) % x.ones);
    // correctness: radix-sorted resolve == per-query
    std::vector<int64_t> a(Q), b(Q); for (long i=0;i<Q;++i) a[i]=x.rs.select(x.k[i]);
    for (long i=0;i<Q;++i) x.kv[i]={x.k[i],(uint32_t)i}; radix_sort(x.kv, x.tmp);
    for (long i=0;i<Q;++i) b[x.kv[i].second]=x.rs.select(x.kv[i].first);
    long bad=0; for(long i=0;i<Q;++i) bad+=(a[i]!=b[i]);
    std::printf("[h10] 512 Mbit, Q=%ld; radix-batch correctness: %s\n", Q, bad?"FAIL":"PASS");
    return x; }();
  return d;
}
static void BM_pq   (benchmark::State& s){ auto&d=data(); for(auto _:s){ for(size_t i=0;i<d.k.size();++i) d.out[i]=d.rs.select(d.k[i]); benchmark::DoNotOptimize(d.out.data()); } s.SetItemsProcessed(s.iterations()*(int64_t)d.k.size()); }
static void BM_std  (benchmark::State& s){ auto&d=data(); for(auto _:s){ d.rs.select_batch(1,d.k.data(),d.out.data(),d.k.size(),&d.kv); benchmark::DoNotOptimize(d.out.data()); } s.SetItemsProcessed(s.iterations()*(int64_t)d.k.size()); }
static void BM_radix(benchmark::State& s){ auto&d=data(); const size_t n=d.k.size(); for(auto _:s){
    for(size_t i=0;i<n;++i) d.kv[i]={d.k[i],(uint32_t)i}; radix_sort(d.kv,d.tmp);
    for(size_t i=0;i<n;++i) d.out[d.kv[i].second]=d.rs.select(d.kv[i].first); benchmark::DoNotOptimize(d.out.data()); } s.SetItemsProcessed(s.iterations()*(int64_t)n); }
BENCHMARK(BM_pq)   ->Name("select/per-query")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_std)  ->Name("select/batch std::sort-pairs")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_radix)->Name("select/batch radix")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
