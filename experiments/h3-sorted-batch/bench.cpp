// H3 — sorting a batch of select queries cuts time vs unsorted (cache locality / HW prefetch),
//       with NO change to the data structure. Real RankAndSelect<1,2>, 512 Mbit (>> L2 -> DRAM).
//
// build (from this dir):
//   clang++ -std=c++20 -O3 -mcpu=native -I .. -I ../../src -I $(brew --prefix)/include \
//           bench.cpp -L $(brew --prefix)/lib -lbenchmark -o bench
// run:  ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true

#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

struct Data {
  BitRS rs; std::vector<uint64_t> packed; uint64_t ones;
  std::vector<int64_t> rnd;   // random select rankings (unsorted)
  std::vector<int64_t> srt;   // same multiset, sorted ascending
};
static Data& data() {
  static Data d = [] {
    Data x;
    x.ones = build_random_bitvector(x.rs, x.packed, 512LL << 20, 0xABCDEFULL);  // 512 Mbit, ~64 MB packed
    const long Q = 2'000'000;
    x.rnd.resize(Q); uint64_t s = 0x777ULL;
    for (long i = 0; i < Q; ++i) x.rnd[i] = (int64_t)(xs(s) % x.ones);
    x.srt = x.rnd; std::sort(x.srt.begin(), x.srt.end());
    std::printf("[h3] RankAndSelect<1,2> over 512 Mbit, %llu ones; Q=%ld queries\n",
                (unsigned long long)x.ones, Q);
    return x;
  }();
  return d;
}

// both read the query array sequentially -> equal array-stream cost; the only difference is the
// LOCALITY of the bitvector index/data accesses driven by sorted-vs-random ranking values.
static void BM_unsorted(benchmark::State& st) {
  auto& d = data(); int64_t acc = 0;
  for (auto _ : st) for (size_t i = 0; i < d.rnd.size(); ++i) acc += d.rs.select(d.rnd[i]);
  st.SetItemsProcessed(st.iterations() * (int64_t)d.rnd.size()); benchmark::DoNotOptimize(acc);
}
static void BM_sorted(benchmark::State& st) {
  auto& d = data(); int64_t acc = 0;
  for (auto _ : st) for (size_t i = 0; i < d.srt.size(); ++i) acc += d.rs.select(d.srt[i]);
  st.SetItemsProcessed(st.iterations() * (int64_t)d.srt.size()); benchmark::DoNotOptimize(acc);
}
BENCHMARK(BM_unsorted)->Name("unsorted")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_sorted)->Name("sorted")->Unit(benchmark::kMillisecond);

int main(int argc, char** argv) { benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
