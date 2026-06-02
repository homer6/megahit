// H1/H2 — select-in-word kernels (Google Benchmark)
//   H1: branchless beats the ctz+shift loop for INDEPENDENT queries (not on a dependent chain).
//   H2: a consteval byte-TABLE select (shift-free) is >= branchless on Apple Silicon.
//
// build (from this dir):
//   clang++ -std=c++20 -O3 -mcpu=native -I .. -I ../../src -I $(brew --prefix)/include \
//           bench.cpp -L $(brew --prefix)/lib -lbenchmark -o bench
// run:  ./bench --benchmark_min_time=0.3s

#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

// independent in-word select queries, sized to stay L1/L2-resident (compute, not memory)
struct Inputs { std::vector<uint64_t> w; std::vector<unsigned> k; };
static const Inputs& inputs() {
  static Inputs in = []{
    Inputs i; uint64_t s = 0xC0FFEEULL; const int N = 1 << 13;  // 8K words ~ 96 KB working set
    i.w.resize(N); i.k.resize(N);
    for (int j = 0; j < N; ++j) {
      uint64_t x = xs(s) & (xs(s) | xs(s)); if (!x) x = 1;       // mixed densities
      i.w[j] = x; i.k[j] = (unsigned)(xs(s) % __builtin_popcountll(x));
    }
    return i;
  }();
  return in;
}

template <unsigned (*F)(uint64_t, unsigned)>
static void BM_throughput(benchmark::State& st) {        // independent queries -> ILP/MLP exposed
  const auto& in = inputs(); unsigned acc = 0;
  for (auto _ : st)
    for (size_t j = 0; j < in.w.size(); ++j) { unsigned r = F(in.w[j], in.k[j]); benchmark::DoNotOptimize(r); acc += r; }
  st.SetItemsProcessed(st.iterations() * (int64_t)in.w.size()); benchmark::DoNotOptimize(acc);
}

template <unsigned (*F)(uint64_t, unsigned)>
static void BM_latency(benchmark::State& st) {           // dependent chain -> single-op latency
  const auto& in = inputs(); const size_t N = in.w.size(); unsigned idx = 0, acc = 0;
  for (auto _ : st)
    for (size_t j = 0; j < N; ++j) { unsigned r = F(in.w[idx], in.k[idx]); idx = (idx + r + 1) & (N - 1); acc += r; }
  st.SetItemsProcessed(st.iterations() * (int64_t)N); benchmark::DoNotOptimize(acc);
}

BENCHMARK(BM_throughput<select_loop>)->Name("throughput/loop");
BENCHMARK(BM_throughput<select_branchless>)->Name("throughput/branchless");
BENCHMARK(BM_throughput<select_table>)->Name("throughput/table");
BENCHMARK(BM_latency<select_loop>)->Name("latency/loop");
BENCHMARK(BM_latency<select_branchless>)->Name("latency/branchless");
BENCHMARK(BM_latency<select_table>)->Name("latency/table");

int main(int argc, char** argv) {
  if (!verify_select_kernels(200000)) { std::fprintf(stderr, "correctness FAILED\n"); return 1; }
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
