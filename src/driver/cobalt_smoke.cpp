// Boost.Cobalt bring-up smoke test for the planned C++23 single-process orchestrator (replacing the Python
// `src/megahit` driver). This proves the vendored-`src/*.cpp`-in-tree build works on macOS / Apple Silicon
// (see docs/boost-cobalt-macos-build.md) and exercises the coroutine machinery we'll orchestrate stages with:
//   - cobalt::main         — the co_await-able entry point (drives the asio::io_context; needs main.cpp)
//   - cobalt::task<T>      — a lazy awaitable "stage"
//   - co_await             — sequencing
//   - cobalt::generator<T> — async generator (std::generator is absent from this libc++; cobalt's is async)
// Build: src/driver/build-cobalt-smoke.sh
#include <boost/cobalt.hpp>

#include <iostream>
#include <vector>

namespace cobalt = boost::cobalt;

// A lazy "stage": does a trivial async unit of work and returns a value.
cobalt::task<int> stage(int id, int in) {
  co_return in + id;  // stand-in for a real assembly stage's result
}

// An async generator — the shape we'd use to stream e.g. a k-list or per-stage events.
cobalt::generator<int> k_list() {
  for (int k : {21, 29, 39, 59, 79, 99}) co_yield k;
  co_return 0;
}

cobalt::main co_main(int /*argc*/, char* /*argv*/[]) {
  // Sequence three "stages" by co_await, threading the result through (like a pipeline).
  int acc = 0;
  for (int i = 1; i <= 3; ++i) acc = co_await stage(i, acc);
  std::cout << "[cobalt-smoke] piped 3 stages -> " << acc << " (expect 6)\n";

  // Drive the async generator.
  std::vector<int> ks;
  auto g = k_list();
  while (true) {
    int k = co_await g;
    if (k == 0) break;
    ks.push_back(k);
  }
  std::cout << "[cobalt-smoke] k-list (" << ks.size() << "):";
  for (int k : ks) std::cout << ' ' << k;
  std::cout << "\n[cobalt-smoke] OK\n";
  co_return 0;
}
