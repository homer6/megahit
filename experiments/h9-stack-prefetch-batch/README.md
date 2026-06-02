# H8/H9 — the batched rank/select API, realized in `RankAndSelect` (C++26)

Implements the proven levers (h3 sorted 3×, h4 prefetch 1.5×) as a **real API** on `kmlib::RankAndSelect`
(`src/kmlib/kmrns.h`): `select_batch` / `rank_batch` (sorted) and `select_batch_prefetch` /
`rank_batch_prefetch` (allocation-free). Answers the C++26 / `inplace_vector` / stack-allocation question.

## What we found (and fixed)

- **H8 (first cut) regressed:** a monolithic sorted batch was *slower* than per-query — the `std::sort` used
  an **indirect comparator** (`ks[a] < ks[b]`), whose random `ks[]` loads thrash cache and cost more than the
  locality buys. **Algorithm bug, not allocation.**
- **H9 fix:** sort **(key,index) pairs directly** (comparator reads `.first` in-place, sequential) — and add
  an **allocation-free prefetch-ahead** variant (no buffer, no sort).

## Evidence — M3 Max, **clang 21, `-std=c++26`**, 2026-06-02 (512 Mbit, 2M queries, 6 reps; correctness PASS)

| API | time | vs per-query | notes |
|---|---:|---:|---|
| `select` per-query | 244 ms | 1.00× | |
| `select_batch` (pair-sort) | **171 ms** | **1.43×** | includes the sort; h3's 3× was sort-*excluded* |
| `select_batch_prefetch` (no alloc) | 229 ms | 1.07× | select's dominant miss is at the *result* position (post-search) → not prefetchable up front |
| `rank` per-query | 35.3 ms | 1.00× | |
| `rank_batch_prefetch` (no alloc) | **23.3 ms** | **1.52×** | rank's miss target (`packed[pos/64]`) is known from the query → prefetch lands |

## C++26 / `inplace_vector` / stack allocation — the answer

- **`-std=c++26` compiles** on clang 21 (incl. linking google-benchmark). **`std::inplace_vector` is NOT in
  clang 21's libc++ yet** (`<inplace_vector>` not found) → use `std::array` for a fixed-capacity stack buffer.
- **But stack-allocating the buffer is not the lever here.** The winning variant (`rank_batch_prefetch`) uses
  **no buffer at all** — allocation is a non-issue and it beats any `inplace_vector` scheme. The sorted variant's
  cost is the **sort**, not the allocation; the reusable `scratch` param already removes per-call `malloc`, and
  a `std::array`/`inplace_vector` stack buffer would change nothing about the 1.43×. (For genuinely large
  in-hand batches a **radix sort** would push it back toward 3× — that's the next lever, not the container.)

## Verdict & consequence

- **`rank_batch_prefetch`: PROVEN — 1.52×, zero-allocation, drop-in.** Use it in the rewrite wherever the
  rank target is known from the query.
- **`select_batch` (pair-sort): PROVEN — 1.43×** including the sort; **the sort algorithm matters** (indirect
  comparator = regression). Radix sort is the path to the full 3×.
- The realized API is correctness-gated (`== per-query`). It's the concrete primitive issue #4 builds on.
