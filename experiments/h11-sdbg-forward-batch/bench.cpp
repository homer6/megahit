// H11 — ForwardBatch on the REAL SDBG: batch the scattered rs_last_.select in SDBG::Forward (the core
// graph-navigation primitive the assemble sweep hammers). Loads the subsample-scale k21 graph (61 MB > L2).
// Validates ForwardBatch == Forward, then benchmarks scalar vs batched.
//
// build:
//   LLVM=/opt/homebrew/opt/llvm/bin/clang++; SDK=$(xcrun --show-sdk-path); P=$(brew --prefix)
//   $LLVM -std=c++26 -O3 -mcpu=native -isysroot "$SDK" -I .. -I ../../src -I "$P/include" \
//     bench.cpp ../../src/sdbg/sdbg_raw_content.cpp ../../src/sdbg/sdbg_meta.cpp -L "$P/lib" -lbenchmark -o bench
#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "sdbg/sdbg.h"
// local RNG (can't use common.hpp here: it uses consteval/C++20, but parallel_hashmap pulled in by
// sdbg.h uses std::result_of, removed in C++20 -> this TU must be C++17).
static inline uint64_t xs(uint64_t& s){ s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }

static const char* kPrefix = "/Users/stevesperandeo/dev/homer6/megahit/profiling/prof_keeptmp/tmp/k21/21";

struct Data {
  SDBG g; std::vector<uint64_t> ids; std::vector<int64_t> out, ref, args;
  std::vector<kmlib::RankAndSelect<1, 2>::BatchPair> scratch;
};
static Data& data() {
  static Data d;  // SDBG holds atomics -> non-movable; build in place, init by reference
  static bool init = [] {
    d.g.LoadFromFile(kPrefix);
    uint64_t N = d.g.size();
    const long Q = 2'000'000; uint64_t s = 0x5DB6ULL;
    d.ids.resize(Q); d.out.assign(Q, 0); d.ref.assign(Q, 0);
    for (long i = 0; i < Q; ++i) d.ids[i] = xs(s) % N;
    for (long i = 0; i < Q; ++i) d.ref[i] = (int64_t)d.g.Forward(d.ids[i]);
    d.g.ForwardBatch(d.ids.data(), d.out.data(), Q, &d.args, &d.scratch);
    long bad = 0; for (long i = 0; i < Q; ++i) bad += (d.out[i] != d.ref[i]);
    std::printf("[h11] loaded SDBG size=%llu (k21, 61 MB); Q=%ld; ForwardBatch correctness: %s\n",
                (unsigned long long)N, Q, bad ? "FAIL" : "PASS");
    return true;
  }();
  (void)init;
  return d;
}
static void BM_scalar(benchmark::State& st){ auto&d=data(); for(auto _:st){ for(size_t i=0;i<d.ids.size();++i) d.out[i]=(int64_t)d.g.Forward(d.ids[i]); benchmark::DoNotOptimize(d.out.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size()); }
static void BM_batch (benchmark::State& st){ auto&d=data(); for(auto _:st){ d.g.ForwardBatch(d.ids.data(), d.out.data(), d.ids.size(), &d.args, &d.scratch); benchmark::DoNotOptimize(d.out.data()); } st.SetItemsProcessed(st.iterations()*(int64_t)d.ids.size()); }
BENCHMARK(BM_scalar)->Name("Forward/scalar")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_batch)->Name("ForwardBatch (radix-sorted select)")->Unit(benchmark::kMillisecond);
int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
