// H4 — software prefetch N queries ahead hides the per-query DRAM miss for random (unsorted) rank,
//      approaching the sorted ceiling WITHOUT needing to sort (useful when queries arrive in-flight).
//      rank(pos) touches packed_array_[pos/64]; we prefetch that line for query i+P.
//
// build: ../build.sh ; run: ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true
#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

struct Data { BitRS rs; std::vector<uint64_t> packed; std::vector<int64_t> rnd, srt; };
static Data& data() {
  static Data d = [] {
    Data x; const int64_t NB = 512LL << 20;
    build_random_bitvector(x.rs, x.packed, NB, 0xABCDEFULL);
    const long Q = 2'000'000; x.rnd.resize(Q); uint64_t s = 0x4242ULL;
    for (long i = 0; i < Q; ++i) x.rnd[i] = (int64_t)(xs(s) % (uint64_t)(NB - 1));
    x.srt = x.rnd; std::sort(x.srt.begin(), x.srt.end());
    std::printf("[h4] 512 Mbit, Q=%ld rank queries\n", Q);
    return x;
  }();
  return d;
}
static void BM_unsorted(benchmark::State& st){ auto&d=data(); int64_t a=0; for(auto _:st) for(auto p:d.rnd) a+=d.rs.rank(p); st.SetItemsProcessed(st.iterations()*(int64_t)d.rnd.size()); benchmark::DoNotOptimize(a);}
static void BM_sorted  (benchmark::State& st){ auto&d=data(); int64_t a=0; for(auto _:st) for(auto p:d.srt) a+=d.rs.rank(p); st.SetItemsProcessed(st.iterations()*(int64_t)d.srt.size()); benchmark::DoNotOptimize(a);}
template <int P> static void BM_prefetch(benchmark::State& st){
  auto&d=data(); const auto&q=d.rnd; const size_t N=q.size(); const uint64_t* base=d.packed.data(); int64_t a=0;
  for(auto _:st) for(size_t i=0;i<N;++i){ if(i+P<N) __builtin_prefetch(base + (size_t)(q[i+P]>>6), 0, 0); a+=d.rs.rank(q[i]); }
  st.SetItemsProcessed(st.iterations()*(int64_t)N); benchmark::DoNotOptimize(a);
}
BENCHMARK(BM_unsorted)->Name("rank/unsorted")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_prefetch<8>) ->Name("rank/unsorted+prefetch8") ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_prefetch<16>)->Name("rank/unsorted+prefetch16")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_prefetch<32>)->Name("rank/unsorted+prefetch32")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_sorted)->Name("rank/sorted (ceiling)")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
