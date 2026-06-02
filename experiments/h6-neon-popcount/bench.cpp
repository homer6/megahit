// H6 — explicit NEON multi-word popcount (vcntq_u8 + widening reduction, 4 accumulators) beats
//      scalar __builtin_popcountll per word for BULK popcount (the build phase: from_packed_array /
//      CountCharInWords). On arm64 scalar popcount already lowers to NEON `cnt` + per-word GPR<->SIMD
//      `fmov` round-trips; staying in vector lanes avoids that churn.
//
// build: ../build.sh ; run: ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true
#include <benchmark/benchmark.h>
#include <arm_neon.h>
#include "common.hpp"
using namespace pe;

static const std::vector<uint64_t>& arr() {        // 64 MB array (8M words), DRAM-resident
  static std::vector<uint64_t> v = [] { std::vector<uint64_t> a(8u << 20); uint64_t s = 0x1357ULL;
    for (auto& x : a) x = xs(s); return a; }();
  return v;
}
static uint64_t pc_scalar(const uint64_t* p, size_t nw) {
  uint64_t t = 0; for (size_t i = 0; i < nw; ++i) t += __builtin_popcountll(p[i]); return t;
}
static uint64_t pc_neon(const uint64_t* p, size_t nw) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(p); size_t nb = nw * 8, i = 0;
  uint64x2_t a0 = vdupq_n_u64(0), a1 = a0, a2 = a0, a3 = a0;
  auto fold = [](uint8x16_t v) { return vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(vcntq_u8(v)))); };
  for (; i + 64 <= nb; i += 64) {
    a0 = vaddq_u64(a0, fold(vld1q_u8(b + i)));
    a1 = vaddq_u64(a1, fold(vld1q_u8(b + i + 16)));
    a2 = vaddq_u64(a2, fold(vld1q_u8(b + i + 32)));
    a3 = vaddq_u64(a3, fold(vld1q_u8(b + i + 48)));
  }
  uint64x2_t s = vaddq_u64(vaddq_u64(a0, a1), vaddq_u64(a2, a3));
  uint64_t t = vgetq_lane_u64(s, 0) + vgetq_lane_u64(s, 1);
  for (size_t w = i / 8; w < nw; ++w) t += __builtin_popcountll(p[w]);
  return t;
}
static void BM_scalar(benchmark::State& st){ const auto&a=arr(); uint64_t r=0; for(auto _:st){ r=pc_scalar(a.data(),a.size()); benchmark::DoNotOptimize(r);} st.SetBytesProcessed(st.iterations()*(int64_t)a.size()*8); }
static void BM_neon  (benchmark::State& st){ const auto&a=arr(); uint64_t r=0; for(auto _:st){ r=pc_neon(a.data(),a.size());   benchmark::DoNotOptimize(r);} st.SetBytesProcessed(st.iterations()*(int64_t)a.size()*8); }
BENCHMARK(BM_scalar)->Name("popcount/scalar")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_neon)->Name("popcount/neon")->Unit(benchmark::kMillisecond);

int main(int argc, char** argv){
  const auto& a = arr();
  if (pc_scalar(a.data(), a.size()) != pc_neon(a.data(), a.size())) { std::fprintf(stderr, "NEON popcount MISMATCH\n"); return 1; }
  std::printf("[h6] popcount correctness PASS (64 MB array)\n");
  benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0;
}
