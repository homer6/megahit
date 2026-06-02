// H5 — a 128-byte INTERLEAVED (rank9/poppy-style) layout co-locating the cumulative count with the
//      bit data in one cache line cuts rank misses from 3 (l2_occ_ + l1_occ_ + packed_array_, three
//      separate std::vectors) to 1 -> faster rank, especially on random (unsorted) access.
//      A/B: current RankAndSelect<1,2>::rank vs an interleaved prototype, unsorted + sorted.
//
// build:  ../build.sh    (Homebrew clang 21)
// run:    ./bench --benchmark_min_time=0.5s --benchmark_repetitions=4 --benchmark_report_aggregates_only=true

#include <benchmark/benchmark.h>
#include "common.hpp"
using namespace pe;

// ---- interleaved rank9-style structure: one 128 B block = [abs count | 15 data words (960 bits)]
struct IRank {
  static constexpr int WPB = 15;            // 1 (abs) + 15 (data) = 16 words = 128 bytes / block
  static constexpr int64_t BPB = WPB * 64;  // 960 bits per block
  struct alignas(128) Block { uint64_t abs; uint64_t w[WPB]; };
  std::vector<Block> blk; int64_t nbits = 0;

  void build(const uint64_t* packed, int64_t nbits_, int64_t nwords) {
    nbits = nbits_;
    int64_t nb = (nwords + WPB - 1) / WPB;
    blk.assign(nb, Block{});
    uint64_t cum = 0;
    for (int64_t b = 0; b < nb; ++b) {
      blk[b].abs = cum;
      for (int w = 0; w < WPB; ++w) {
        int64_t gi = b * WPB + w;
        uint64_t word = (gi < nwords) ? packed[gi] : 0;
        blk[b].w[w] = word; cum += __builtin_popcountll(word);
      }
    }
  }
  // inclusive rank: number of set bits in [0 .. pos]  (matches RankAndSelect<1,2>::rank)
  int64_t rank(int64_t pos) const {
    int64_t b = pos / BPB; int64_t rem = pos - b * BPB; int wi = (int)(rem >> 6); int bit = (int)(rem & 63);
    const Block& B = blk[b];                       // single cache line touched
    int64_t r = (int64_t)B.abs;
    for (int w = 0; w < wi; ++w) r += __builtin_popcountll(B.w[w]);
    uint64_t mask = (bit == 63) ? ~0ULL : ((1ULL << (bit + 1)) - 1);
    return r + __builtin_popcountll(B.w[wi] & mask);
  }
};

struct Data {
  BitRS rs; std::vector<uint64_t> packed; uint64_t ones; IRank ir;
  std::vector<int64_t> rnd, srt;            // random positions in [0, nbits)
};
static Data& data() {
  static Data d = [] {
    Data x; const int64_t NBITS = 512LL << 20;
    x.ones = build_random_bitvector(x.rs, x.packed, NBITS, 0xABCDEFULL);
    x.ir.build(x.packed.data(), NBITS, (int64_t)x.packed.size());
    const long Q = 2'000'000; x.rnd.resize(Q); uint64_t s = 0x999ULL;
    for (long i = 0; i < Q; ++i) x.rnd[i] = (int64_t)(xs(s) % (uint64_t)(NBITS - 1));
    x.srt = x.rnd; std::sort(x.srt.begin(), x.srt.end());
    // correctness: interleaved rank == reference rank, on random positions
    long bad = 0; uint64_t cs = 0xBEEF;
    for (int t = 0; t < 1000000; ++t) { int64_t p = (int64_t)(xs(cs) % (uint64_t)(NBITS - 1));
      if (x.ir.rank(p) != x.rs.rank(p)) { ++bad; if (bad < 4) std::printf("  rank mismatch @ %lld: ir=%lld rs=%lld\n",(long long)p,(long long)x.ir.rank(p),(long long)x.rs.rank(p)); } }
    std::printf("[h5] 512 Mbit, %llu ones; interleaved blocks=%zu (%.0f MB); rank correctness: %s\n",
      (unsigned long long)x.ones, x.ir.blk.size(), x.ir.blk.size()*128.0/1e6, bad ? "FAIL" : "PASS");
    return x;
  }();
  return d;
}

static void BM_cur_unsorted(benchmark::State& s){ auto&d=data(); int64_t a=0; for(auto _:s) for(auto p:d.rnd) a+=d.rs.rank(p); s.SetItemsProcessed(s.iterations()*(int64_t)d.rnd.size()); benchmark::DoNotOptimize(a);}
static void BM_cur_sorted(benchmark::State& s){ auto&d=data(); int64_t a=0; for(auto _:s) for(auto p:d.srt) a+=d.rs.rank(p); s.SetItemsProcessed(s.iterations()*(int64_t)d.srt.size()); benchmark::DoNotOptimize(a);}
static void BM_il_unsorted(benchmark::State& s){ auto&d=data(); int64_t a=0; for(auto _:s) for(auto p:d.rnd) a+=d.ir.rank(p); s.SetItemsProcessed(s.iterations()*(int64_t)d.rnd.size()); benchmark::DoNotOptimize(a);}
static void BM_il_sorted(benchmark::State& s){ auto&d=data(); int64_t a=0; for(auto _:s) for(auto p:d.srt) a+=d.ir.rank(p); s.SetItemsProcessed(s.iterations()*(int64_t)d.srt.size()); benchmark::DoNotOptimize(a);}
BENCHMARK(BM_cur_unsorted)->Name("rank/current/unsorted")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_il_unsorted )->Name("rank/interleaved/unsorted")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_cur_sorted  )->Name("rank/current/sorted")->Unit(benchmark::kMillisecond);
BENCHMARK(BM_il_sorted   )->Name("rank/interleaved/sorted")->Unit(benchmark::kMillisecond);

int main(int argc, char** argv){ benchmark::Initialize(&argc, argv); benchmark::RunSpecifiedBenchmarks(); return 0; }
