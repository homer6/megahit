// experiments/common.hpp — shared, *verified* infrastructure for rank/select
// optimization experiments (MEGAHIT macOS/Apple-Silicon performance fork).
//
// Every experiment includes this so kernels, timing, RNG, correctness, and the
// real-structure builder are identical across experiments — results can't drift.
//
// build an experiment:  clang++ -std=c++20 -O3 -mcpu=native -I ../../src exp.cpp -o exp
// (consteval requires C++20; -mcpu=native targets the host Apple core; -I../../src for kmlib/)

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <mach/mach_time.h>
#include "kmlib/kmrns.h"

namespace pe {  // performance-engineering shared infra

// ----------------------------------------------------------------- timing (mach)
inline double ns_per_tick() {
  static double v = 0;
  if (v == 0) { mach_timebase_info_data_t tb; mach_timebase_info(&tb); v = (double)tb.numer / tb.denom; }
  return v;
}
inline uint64_t tick() { return mach_absolute_time(); }
inline double ns(uint64_t dt) { return dt * ns_per_tick(); }

// ----------------------------------------------------------------- RNG (deterministic xorshift)
inline uint64_t xs(uint64_t& s) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }

// dead-code-elimination sink
inline volatile uint64_t g_sink;

// ----------------------------------------------------------------- consteval select-in-byte tables
// SELT.pc[b]      = popcount of byte b
// SELT.sel[b][r]  = bit position (0..7) of the r-th (0-based) set bit in byte b, else 8
struct SelTables { uint8_t pc[256]; uint8_t sel[256][8]; };
consteval SelTables make_sel_tables() {
  SelTables t{};
  for (int b = 0; b < 256; ++b) {
    int c = 0;
    for (int i = 0; i < 8; ++i) t.sel[b][i] = 8;
    for (int i = 0; i < 8; ++i) if (b & (1 << i)) t.sel[b][c++] = (uint8_t)i;
    t.pc[b] = (uint8_t)c;
  }
  return t;
}
inline constexpr SelTables SELT = make_sel_tables();  // baked into .rodata at compile time

// ----------------------------------------------------------------- select-in-word kernels (0-based k)
// All return the bit position of the (k+1)-th set bit in x. Assume k < popcount(x).

// (A) baseline: the arm64 #else path of kmrns.h::SelectInWord — O(num_c) ctz+shift loop
inline unsigned select_loop(uint64_t x, unsigned k) {
  unsigned num_c = k + 1, tz = 0;
  while (num_c > 0) { tz = __builtin_ctzll(x); x ^= 1ULL << tz; --num_c; }
  return tz;
}

// (B) branchless: constant 6-step binary-search select (shift-based, no data-dependent branch)
inline unsigned select_branchless(uint64_t x, unsigned k) {
  unsigned pos = 0, c;
  c = __builtin_popcountll(x & 0x00000000FFFFFFFFULL); if (c <= k) { k -= c; x >>= 32; pos += 32; }
  c = __builtin_popcountll(x & 0x000000000000FFFFULL); if (c <= k) { k -= c; x >>= 16; pos += 16; }
  c = __builtin_popcountll(x & 0x00000000000000FFULL); if (c <= k) { k -= c; x >>=  8; pos +=  8; }
  c = __builtin_popcountll(x & 0x000000000000000FULL); if (c <= k) { k -= c; x >>=  4; pos +=  4; }
  c = __builtin_popcountll(x & 0x0000000000000003ULL); if (c <= k) { k -= c; x >>=  2; pos +=  2; }
  c = (unsigned)(x & 1ULL);                            if (c <= k) {          pos +=  1; }
  return pos;
}

// (C) consteval byte-table: SHIFT-FREE in the bit-find — walk bytes via a byte view,
//     popcount/position come from the compile-time tables. Fixed 7-step branchless walk.
inline unsigned select_table(uint64_t x, unsigned k) {
  const uint8_t* B = reinterpret_cast<const uint8_t*>(&x);  // little-endian byte view
  unsigned byte = 0;
  for (int s = 0; s < 7; ++s) { unsigned c = SELT.pc[B[byte]]; unsigned adv = (k >= c); k -= adv ? c : 0; byte += adv; }
  return byte * 8u + SELT.sel[B[byte]][k];
}

// ground truth
inline unsigned select_naive(uint64_t x, unsigned k) {
  for (unsigned i = 0; i < 64; ++i) if ((x >> i) & 1) { if (k-- == 0) return i; }
  return 64;
}

// verify all three kernels == naive over random words + low-bit edge cases
inline bool verify_select_kernels(long iters = 2000000) {
  uint64_t s = 0x1234567789abcdefULL; long n = 0;
  for (long t = 0; t < iters; ++t) {
    uint64_t x = (t < 64) ? (t == 0 ? 0 : (1ULL << t) - 1) : xs(s);
    unsigned pc = __builtin_popcountll(x);
    for (unsigned k = 0; k < pc; ++k) {
      unsigned a = select_naive(x, k);
      if (select_loop(x, k) != a || select_branchless(x, k) != a || select_table(x, k) != a) {
        std::printf("MISMATCH x=%016llx k=%u naive=%u loop=%u bw=%u tbl=%u\n",
          (unsigned long long)x, k, a, select_loop(x,k), select_branchless(x,k), select_table(x,k));
        return false;
      }
      ++n;
    }
  }
  std::printf("[common] select kernels verified: %ld (word,k) cases  (loop==branchless==table==naive)\n", n);
  return true;
}

// ----------------------------------------------------------------- real-structure builder
using BitRS = kmlib::RankAndSelect<1, 2>;   // the rs_last_ "last" bitvector type

// fill `packed` with ~25% density and build `rs` over it; returns #ones. `packed` must outlive `rs`.
inline uint64_t build_random_bitvector(BitRS& rs, std::vector<uint64_t>& packed, int64_t nbits, uint64_t seed) {
  int64_t nw = nbits / 64;
  packed.assign(nw, 0);
  uint64_t s = seed ? seed : 1, ones = 0;
  for (int64_t i = 0; i < nw; ++i) { uint64_t w = xs(s) & xs(s); packed[i] = w; ones += __builtin_popcountll(w); }
  rs.from_packed_array(packed.data(), nbits);
  return ones;
}

}  // namespace pe
