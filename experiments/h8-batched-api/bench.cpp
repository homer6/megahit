// H8 — validate the real RankAndSelect::select_batch API: (1) bit-identical to n separate select()
//      calls, (2) faster INCLUDING its own sort cost (the honest in-API number, vs h3 which excluded sort).
//
// build: ../build.sh ; run: ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true
#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

struct Data {
  BitRS rs; std::vector<uint64_t> packed; uint64_t ones;
  std::vector<int64_t> k;                    // random rankings
  std::vector<int64_t> out_q, out_b;         // per-query / batched results
  std::vector<uint32_t> scratch;             // reusable permutation buffer for the batched API
};
static Data& data() {
  static Data d = [] {
    Data x; x.ones = build_random_bitvector(x.rs, x.packed, 512LL << 20, 0xABCDEFULL);
    const long Q = 2'000'000; x.k.resize(Q); x.out_q.assign(Q, 0); x.out_b.assign(Q, 0);
    uint64_t s = 0xB47C4EDULL; for (long i = 0; i < Q; ++i) x.k[i] = (int64_t)(xs(s) % x.ones);
    // correctness: batched == per-query
    for (long i = 0; i < Q; ++i) x.out_q[i] = x.rs.select(x.k[i]);
    x.rs.select_batch(1, x.k.data(), x.out_b.data(), (size_t)Q, &x.scratch);
    long bad = 0; for (long i = 0; i < Q; ++i) if (x.out_q[i] != x.out_b[i]) ++bad;
    std::printf("[h8] 512 Mbit, %llu ones, Q=%ld; select_batch correctness: %s\n",
                (unsigned long long)x.ones, Q, bad ? "FAIL" : "PASS");
    return x;
  }();
  return d;
}
static void BM_perquery(benchmark::State& st){ auto&d=data(); for(auto _:st){ for(size_t i=0;i<d.k.size();++i) d.out_q[i]=d.rs.select(d.k[i]); benchmark::DoNotOptimize(d.out_q.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.k.size()); }
static void BM_batched (benchmark::State& st){ auto&d=data(); for(auto _:st){ d.rs.select_batch(1, d.k.data(), d.out_b.data(), d.k.size(), &d.scratch); benchmark::DoNotOptimize(d.out_b.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.k.size()); }
BENCHMARK(BM_perquery)->Name("select/per-query (unsorted)")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_batched)->Name("select_batch (sort+resolve, incl. sort)")->Unit(benchmark::kMillisecond);

int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
