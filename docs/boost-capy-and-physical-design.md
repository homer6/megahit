<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# Explain Boost.Capy in detail

I now have enough information to write a comprehensive explanation of Boost.Capy. Let me compile this into a well-structured answer.

## What is Boost.Capy?

**Boost.Capy** is a C++20 coroutine-only I/O foundation library developed by The C++ Alliance (cppalliance). It is not a networking library — it provides no sockets, no HTTP, and no DNS — but it is the core infrastructure layer upon which coroutine-based networking libraries like Boost.Corosio, Boost.Http, and Boost.Beast2 are built. Its central design insight, attributed to Peter Dimov, is that *"an API designed from the ground up to use C++20 coroutines can achieve performance and ergonomics which cannot otherwise be obtained."*[^1_1][^1_2]

***

## What Capy Does (and Doesn't Do)

Capy handles the *mechanics* of coroutine-based I/O without being tied to any platform:[^1_3][^1_1]

**It provides:**

- Lazy coroutine tasks (`task<T>`) with forward-propagating stop tokens and automatic cancellation
- Buffer types and buffer sequences (taken from Asio and improved)
- Stream concepts: `ReadStream`, `WriteStream`, `ReadSource`, `WriteSink`, `BufferSource`, `BufferSink`
- Type-erased streams: `any_stream`, `any_read_stream`, `any_write_stream` for fast compilation
- Concurrency primitives: executors, strands, thread pools, `when_all`, `when_any`
- The **IoAwaitable protocol** for context propagation
- Test utilities: mock streams, mock sources/sinks, error injection

**It does NOT provide:**

- Networking (sockets, acceptors, DNS) — that is Boost.Corosio's job
- Protocols (HTTP, WebSocket, TLS) — covered by Boost.Http, Boost.Beast2
- Platform event loops (io_uring, IOCP, epoll, kqueue)
- Callbacks or futures — coroutine-only means no other continuation styles
- Sender/receiver (it uses IoAwaitable, not `std::execution`)

***

## The IoAwaitable Protocol

The most important design innovation in Capy is the **IoAwaitable protocol** — an extension to the standard C++20 awaiter interface. Standard awaitables only receive a coroutine handle at suspension:[^1_4]

```cpp
void await_suspend(std::coroutine_handle<> h);
```

But I/O code needs three pieces of context: *where to dispatch completions* (executor), *whether to cancel* (stop token), and *how to allocate memory* (allocator). Capy solves this by extending `await_suspend` to carry this context forward explicitly:[^1_4]

```cpp
coro await_suspend(coro h, executor_ref ex, std::stop_token token);
```

When you `co_await` a child task inside a Capy `task<T>`, the parent's `await_transform` intercepts the call and automatically passes its executor and stop token down to the child — no manual wiring required. This *forward propagation* model keeps awaitables decoupled from the calling coroutine's promise type, enabling any `IoAwaitable` to work with any `IoAwaitableTask`.[^1_4]

***

## Tasks: `task<T>`

Capy's `task<T>` is a **lazy coroutine type** — it does not begin execution until `co_await`-ed or explicitly launched. Key properties:[^1_1]

- **Lazy** — no execution until awaited
- **Stop-token aware** — carries a `std::stop_token` automatically forwarded to child awaitables
- **`[[nodiscard]]`** — dropping a task without awaiting it is a compile-time warning
- **HALO-eligible** — marked with `BOOST_CAPY_CORO_AWAIT_ELIDABLE` to enable the *Heap Allocation eLision Optimization*, where the compiler can stack-allocate coroutine frames for immediately-awaited tasks[^1_5]

The `IoAwaitableTask` and `IoLaunchableTask` concepts define the additional promise methods (`set_executor`, `stop_token`, `handle`, `result`, etc.) that the task must provide for use with Capy's launcher API.[^1_4]

***

## Launching Tasks: `run_async` and `run`

Capy provides two launchers with a distinctive **two-phase invocation** syntax `f(context)(task)`:[^1_6]


| Launcher | Context | Usage |
| :-- | :-- | :-- |
| `run_async(ex)(my_task())` | Non-coroutine code (`main`, callbacks) | Fire-and-forget |
| `co_await run(ex)(my_task())` | Inside another coroutine | Awaitable, returns result |

The unusual double-call syntax is not aesthetic — it is a **mechanical correctness requirement**. Coroutine frame allocation (`operator new`) happens *before* the coroutine body executes. C++17 guarantees that in a postfix expression `f(ctx)(task)`, `f(ctx)` evaluates *first*, allowing Capy to set a thread-local allocator pointer before `task()` triggers `operator new` on the coroutine frame. A single-call form like `run_async(ex, my_task())` would allocate the frame with the wrong allocator — a silent correctness bug, not just a style issue.[^1_6][^1_5]

```cpp
// run_async launches from non-coroutine code
run_async(ioc.get_executor())(echo_session(std::move(peer)));

// run switches executors inside a coroutine
int result = co_await run(worker_ex)(cpu_bound_task());
```


***

## Concurrent Composition: `when_all` and `when_any`

Capy provides structured concurrency through two composition primitives:[^1_7]

**`when_all`** launches multiple tasks concurrently and waits for all of them. It returns a tuple of results, filtering out `void` tasks:

```cpp
auto [a, b, c] = co_await when_all(fetch_a(), fetch_b(), fetch_c());
```

If any task throws, `when_all` captures the exception, requests stop on all sibling tasks (via stop tokens), waits for them all to complete or respond, then rethrows the first exception.[^1_7]

**`when_any`** is the counterpart — it resolves as soon as any one task completes and cancels the rest. Both primitives automatically propagate stop tokens, making cancellation composable.[^1_1][^1_7]

***

## Buffer Model

Capy's buffer system is taken from Boost.Asio and refined. Two fundamental types exist:[^1_8][^1_1]

- **`const_buffer`** — a read-only view of a memory region
- **`mutable_buffer`** — a writable view of a memory region

The **`ConstBufferSequence`** and **`MutableBufferSequence`** concepts accept anything iterable over compatible buffer types, enabling scatter/gather I/O with zero extra allocation:[^1_8]

```cpp
std::array<capy::mutable_buffer, 2> bufs = {
    capy::mutable_buffer(header, header_size),
    capy::mutable_buffer(body, body_size)
};
co_await sock.read_some(bufs);
```

Higher-level buffer types like `circular_dynamic_buffer` and `consuming_buffers` are also provided for use in protocol parsers.[^1_9]

***

## Stream Concepts and Type Erasure

Capy defines a layered hierarchy of stream concepts:[^1_10]

- **`ReadStream`** / **`WriteStream`** — partial I/O: `read_some` / `write_some` that may transfer fewer bytes than requested
- **`ReadSource`** / **`WriteSink`** — complete I/O: fills or flushes buffers entirely, signals EOF
- **`BufferSource`** / **`BufferSink`** — higher-level concepts for pipelines

Any type satisfying `ReadStream` — a TCP socket, a TLS stream, a file handle, or an in-memory mock — can be used interchangeably in generic algorithms written against the concept. Capy also provides **`any_stream`**, **`any_read_stream`**, and **`any_write_stream`** — type-erased wrappers that hide the concrete stream type behind a virtual interface, trading a small indirection cost for dramatically faster compile times and the ability to store streams polymorphically.[^1_3][^1_1][^1_4]

***

## Memory Management

Frame allocation is customizable through the `FrameAllocator` concept, which any standard C++ allocator satisfies. By default, Capy uses a `recycling_memory_resource` — a custom memory resource that maintains per-size-class freelists, reusing recently freed coroutine frames for cache-friendly allocation. For batch workloads, a `std::pmr::monotonic_buffer_resource` can be passed to `run_async` to allocate all frames from a single arena, with bulk deallocation at the arena's end of life.[^1_5]

***

## Design Philosophy and Position in the Stack

Capy sits at the *narrow waist* of the C++ Alliance's I/O library stack — it is the lower-level, platform-free layer that everything else builds on. It is pure C++20 with no `#ifdef _WIN32`, no file descriptors, and no platform event loops. It can compile on embedded systems, WebAssembly, and platforms without network sockets.[^1_11][^1_3]

The separation from Boost.Corosio (which handles actual platform networking) is rooted in **physical design principles** from John Lakos' work on large-scale C++: acyclic component dependencies keep Cumulative Component Dependency (CCD) low, ensuring that a library needing only buffer algorithms does not transitively link against IOCP on Windows or epoll on Linux. The existence of Boost.Http — a complete HTTP/1.1 parser written *entirely* against Capy with no dependency on Corosio whatsoever — validates this layered approach in practice.[^1_3]
<span style="display:none">[^1_12][^1_13][^1_14][^1_15][^1_16][^1_17][^1_18][^1_19][^1_20][^1_21][^1_22][^1_23][^1_24][^1_25][^1_26][^1_27][^1_28][^1_29][^1_30][^1_31][^1_32][^1_33][^1_34][^1_35][^1_36][^1_37][^1_38][^1_39][^1_40][^1_41][^1_42][^1_43][^1_44][^1_45][^1_46][^1_47][^1_48][^1_49][^1_50][^1_51][^1_52][^1_53][^1_54][^1_55][^1_56][^1_57][^1_58][^1_59][^1_60][^1_61][^1_62][^1_63][^1_64][^1_65][^1_66][^1_67][^1_68]</span>

<div align="center">⁂</div>

[^1_1]: https://246.capy.prtest3.cppalliance.org/capy/index.html

[^1_2]: https://github.com/cppalliance/capy

[^1_3]: https://www.vinniefalco.com/p/why-capy-is-separate

[^1_4]: https://master.capy.cpp.al/capy/coroutines/io-awaitable.html

[^1_5]: https://master.capy.cpp.al/capy/4.coroutines/4g.allocators.html

[^1_6]: https://master.capy.cpp.al/capy/8.design/8l.RunApi.html

[^1_7]: https://master.capy.cpp.al/capy/coroutines/composition.html

[^1_8]: https://master.capy.cpp.al/capy/buffers/sequences.html

[^1_9]: https://152.corosio.prtest3.cppalliance.org/corosio/4.guide/4n.buffers.html

[^1_10]: https://master.capy.cpp.al/capy/6.streams/6c.sources-sinks.html

[^1_11]: https://blog.csdn.net/TM1695648164/article/details/149351296

[^1_12]: https://246.capy.prtest3.cppalliance.org/capy/8.examples/8.intro.html

[^1_13]: https://github.com/capy-language/capy

[^1_14]: https://152.corosio.prtest3.cppalliance.org/corosio/index.html

[^1_15]: https://www.boost.org/libs/program_options

[^1_16]: https://github.com/caiorss/C-Cpp-Notes/blob/master/boost-libraries.org

[^1_17]: https://corosio.org

[^1_18]: https://github.com/cppalliance/corosio

[^1_19]: https://www.boost.org

[^1_20]: https://www.boost.org/libs/cobalt

[^1_21]: https://github.com/cppalliance/capy/blob/develop/include/boost/capy/task.hpp

[^1_22]: https://github.com/cppalliance/rts

[^1_23]: https://grafikrobot.github.io/b2doc/

[^1_24]: https://github.com/boostorg/boost/issues/1023

[^1_25]: https://stackoverflow.com/questions/76351432/boost-asio-how-to-use-strands-with-c20-coroutines

[^1_26]: https://pkg.go.dev/github.com/rodent-software/capy

[^1_27]: https://stackoverflow.com/questions/76097334/boostasioawaitable-that-always-returns-to-caller-strand

[^1_28]: https://capy-ui.org/docs/getting-started/installation/

[^1_29]: https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/strands.html

[^1_30]: https://github.com/capy-ui/documentation

[^1_31]: https://152.corosio.prtest3.cppalliance.org/corosio/3.tutorials/3b.http-client.html

[^1_32]: https://www.bcgsc.ca/downloads/morinlab/UMontreal_MLLT/renv/library/R-4.1/x86_64-pc-linux-gnu/BH/include/boost/asio/impl/awaitable.hpp

[^1_33]: https://boostorg.github.io/cobalt/coroutine_primer.html

[^1_34]: https://www.boost.org/doc/libs/1_84_0/boost/asio/strand.hpp

[^1_35]: https://github.com/cppalliance

[^1_36]: https://www.boost.org/doc/libs/1_88_0/libs/outcome/doc/html/tutorial/essential/coroutines/awaitables.html

[^1_37]: https://github.com/cppalliance/cppalliance.github.io

[^1_38]: https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/overview/core/cancellation.html

[^1_39]: https://github.com/boostorg/cobalt

[^1_40]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.html

[^1_41]: https://cppalliance.org/r/wg21/p4003r0-reddit.html

[^1_42]: https://github.com/usecapy

[^1_43]: https://beta.boost.org/doc/libs/1_80_0/doc/html/boost_asio/overview/core/cancellation.html

[^1_44]: https://isocpp.org/files/papers/P4089R0.pdf

[^1_45]: https://github.com/cppalliance/crypt

[^1_46]: https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/core/cancellation.html

[^1_47]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4003r0.pdf

[^1_48]: https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/mysql/tutorial_async.html

[^1_49]: https://www.npmjs.com/package/@cppalliance/antora-cpp-reference-extension

[^1_50]: https://beta.boost.org/doc/libs/1_75_0/doc/html/boost_asio/overview/core/spawn.html

[^1_51]: https://cppalliance.org/proposals.html

[^1_52]: https://github.com/cppalliance/http

[^1_53]: https://cppalliance.org

[^1_54]: https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/composition/cpp20_coroutines.html

[^1_55]: https://docs.alliancecan.ca/wiki/C++

[^1_56]: https://x.com/CPPAlliance

[^1_57]: https://stackoverflow.com/questions/30848606/how-to-safely-cancel-a-task-using-a-cancellationtoken-and-await-task-whenall?rq=2

[^1_58]: https://arxiv.org/html/2502.07432v1

[^1_59]: https://master.capy.cpp.al/capy/7.examples/7.intro.html

[^1_60]: https://open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3409r0.html

[^1_61]: https://github.com/MicrosoftDocs/cpp-docs/blob/main/docs/parallel/concrt/cancellation-in-the-ppl.md

[^1_62]: https://github.com/capy-ui/capy/blob/master/README.md

[^1_63]: https://isocpp.org/files/papers/P4100R0.pdf

[^1_64]: https://learn.microsoft.com/en-us/cpp/parallel/concrt/cancellation-in-the-ppl?view=msvc-170

[^1_65]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4100r0.pdf

[^1_66]: https://cpp-rendering.io/c-type-erasure-implementation-a-low-boilerplate-approach/

[^1_67]: https://docs.rs/cpal/latest/cpal/platform/struct.Stream.html

[^1_68]: https://open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4003r2.pdf


---

```
# <q>The separation from Boost.Corosio (which handles actual platform networking) is rooted in physical design principles from John Lakos’ work on large-scale C++: acyclic component dependencies keep Cumulative Component Dependency (CCD) low, ensuring that a library needing only buffer algorithms does not transitively link against IOCP on Windows or epoll on Linux. The existence of Boost.Http — a complete HTTP/1.1 parser written entirely against Capy with no dependency on Corosio whatsoever — validates this layered approach in practice.[vinniefalco]</q> explain this concept in detail
```

Answer skipped.
<span style="display:none">[^2_1][^2_10][^2_11][^2_12][^2_13][^2_14][^2_15][^2_16][^2_17][^2_18][^2_19][^2_2][^2_20][^2_21][^2_22][^2_23][^2_24][^2_25][^2_26][^2_27][^2_28][^2_29][^2_3][^2_30][^2_4][^2_5][^2_6][^2_7][^2_8][^2_9]</span>

<div align="center">⁂</div>

[^2_1]: https://stackoverflow.com/questions/45783173/acyclic-dependency-principle-how-could-component-dependency-cycles-be-reason-f

[^2_2]: https://www.boost.org/doc/contributor-guide/design-guide/dependencies.html

[^2_3]: https://www.reddit.com/r/cpp/comments/4yx13h/boost_dependencies_and_bcp/

[^2_4]: https://github.com/cppalliance/rts

[^2_5]: https://devlead.io/DevTips/AcyclicDependenciesPrinciple

[^2_6]: https://stackoverflow.com/questions/173374/how-do-you-deal-with-large-dependencies-in-boost

[^2_7]: https://stackoverflow.com/questions/10818717/how-to-find-out-what-dependencies-i-e-other-boost-libraries-a-particular-boost

[^2_8]: https://khalilstemmler.com/wiki/acyclic-dependencies-principle/

[^2_9]: https://www.reddit.com/r/cpp/comments/18pioj9/annoyed_with_overuse_of_boost_in_c_discussionsrant/

[^2_10]: https://github.com/cppalliance/http

[^2_11]: https://learn.microsoft.com/en-us/archive/msdn-magazine/2009/april/parallelizing-operations-with-dependencies

[^2_12]: https://boost-ext.github.io/di/

[^2_13]: https://cppalliance.org/mohammad/2024/10/25/MohammadsQ3Update.html

[^2_14]: https://cppalliance.org/ruben/2026/04/06/Ruben2026Q1Update.html

[^2_15]: https://doc.cgal.org/latest/Manual/thirdparty.html

[^2_16]: https://javadoc.io/doc/com.tngtech.archunit/archunit/0.23.1/com/tngtech/archunit/library/metrics/LakosMetrics.html

[^2_17]: https://blog.hello2morrow.com/2014/12/assess-and-control-component-coupling-with-sonargraph-explorer/

[^2_18]: https://rsf.github.io/wiki/Wiki7132.html?page=Levelization

[^2_19]: https://baruzzo.wordpress.com/2009/08/22/how-testable-is-a-software-architecture/

[^2_20]: https://github.com/OlegAlexander/lakos/blob/master/README.md

[^2_21]: https://www.oreilly.com/library/view/large-scale-c-volume/9780133927573/

[^2_22]: https://swiftalyzer.com/understanding-your-project-with-metrics-ccd/

[^2_23]: https://dzone.com/refcardz/designing-quality-software

[^2_24]: https://www.goodreads.com/book/show/6626982-large-scale-c

[^2_25]: https://www.steti.info/blog/how-to-calculate-average-component-dependency-acd

[^2_26]: https://accu.org/conf-docs/PDFs_Autumn_2019/John_Lakos_-_C++_Modules_and_Large-Scale_Development.pdf

[^2_27]: http://www.cs.unc.edu/~stotts/COMP204/lakos/

[^2_28]: https://artofcode.info/2023/10/26/how-to-calculate-average-component-dependency-acd/

[^2_29]: https://www.youtube.com/watch?v=K_fTl_hIEGY

[^2_30]: https://books.google.fr/books?id=AuMpAQAAMAAJ


---

# no, the design principle

You’re asking about the **design principle**, not Boost.Capy specifically: the idea is that large software should be organized so physical dependencies form a one-way, acyclic graph, with low cumulative dependency cost. In Lakos-style physical design, this is about controlling what must be compiled, tested, linked, understood, or deployed when you use any one component.

## Physical design

In this context, “physical design” means how code is divided into files, packages, libraries, modules, and build units—not the abstract class model or runtime behavior. Lakos’ work treats physical structure as a first-class design problem because, in large C++ systems, compile time, link time, testability, and deployability are strongly affected by include and link dependencies.[^3_1][^3_2][^3_3]

A simple example: if a JSON parser depends only on strings and buffers, it is physically lightweight; if it also depends on sockets, TLS, threads, filesystem, and platform event loops, then every user of the parser inherits that dependency burden even when they only want parsing.

## Acyclic dependencies

An acyclic dependency graph means dependencies point in one direction and never form loops; Lakos-style guidance explicitly says to avoid cyclic physical dependencies among components. If component A depends on B, and B depends on A, neither can be understood, compiled, tested, or reused independently in a clean order.[^3_4]

The goal is **levelization**: place components into levels so higher-level components may depend on lower-level ones, but lower-level components never depend back upward. In practice, “buffers” might be lower level than “streams,” “streams” lower than “TCP sockets,” and “HTTP client” higher than sockets; reversing that dependency would make the foundation depend on the building.[^3_5]

## CCD meaning

CCD, or Cumulative Component Dependency, is a metric for how much dependency weight exists in a component graph; one definition is the sum, over all components, of the number of components needed to test each one incrementally. ArchUnit’s Lakos metrics describe CCD as the sum of all `dependsOn` values across components, with ACD as CCD divided by the number of components.[^3_6][^3_5]

Here is the intuition:


| Component | Direct/indirect dependencies | Cost to test/use |
| :-- | --: | --: |
| Buffer | none | 1 component |
| Stream | Buffer | 2 components |
| Socket | Stream, Buffer, OS backend | 4+ components |
| HTTP client | Socket, Stream, Buffer, parser, TLS maybe | many components |

Low CCD means most components can be used or tested with few prerequisites; high CCD means even small pieces drag in much of the system.

## Why cycles are bad

Cycles collapse multiple components into one effective component, because none of them can stand independently. If parser depends on transport, and transport depends on parser, then you cannot reuse the parser without transport or transport without parser.

Cycles also make change impact harder to predict. A change in one part may force rebuilds, retests, or redesigns across the entire cycle, while an acyclic graph confines change pressure mostly upward.

## Why “narrow waist” helps

A good large-system architecture often has a small, stable lower layer—a “narrow waist”—that many higher layers can depend on without depending on each other. In the Capy/Corosio/HTTP example, Capy is the narrow waist: protocols and transports can share common coroutine, buffer, and stream abstractions without forcing HTTP parsing to depend on OS networking.

This is the key design rule: put stable, general abstractions lower; put platform-specific or policy-heavy mechanisms higher. That lets a library needing only buffers avoid inheriting epoll, IOCP, sockets, TLS, or thread-pool dependencies.

## Small diagram

```text
Good physical design: acyclic, levelized

Level 3:   HTTP client/server
              |
Level 2:   Corosio sockets / TLS / platform I/O
              |
Level 1:   Capy streams / tasks / buffers
              |
Level 0:   C++ standard library


Bad physical design: cycle

HTTP parser  <---->  Socket transport
     ^                  |
     |                  v
   Buffers  <------ Platform I/O
```

The good graph lets you test and reuse lower levels independently. The bad graph turns conceptually separate parts into one tangled physical unit.

## Applied takeaway

The design principle is: **separate by physical dependency cost, not just by conceptual category**. If a lower-level abstraction can be useful without a higher-level facility, the lower layer must not depend on that higher layer.

So the Capy/Corosio split is an example of a broader rule: keep general mechanisms like buffers, task types, stream concepts, and coroutine context propagation below concrete networking; then networking can depend on them, but parsers, tests, mocks, embedded builds, and non-network transports do not have to depend on networking at all.
<span style="display:none">[^3_10][^3_11][^3_12][^3_13][^3_14][^3_15][^3_16][^3_17][^3_7][^3_8][^3_9]</span>

<div align="center">⁂</div>

[^3_1]: https://www.oreilly.com/library/view/large-scale-c-volume/9780133927573/ch03.xhtml

[^3_2]: http://www.cs.unc.edu/~stotts/COMP204/lakos/

[^3_3]: https://www.oreilly.com/library/view/large-scale-c-volume/9780133927573/

[^3_4]: http://www.cs.unc.edu/~stotts/COMP204/lakos/guide.html

[^3_5]: https://rsf.github.io/wiki/Wiki7132.html?page=Levelization

[^3_6]: https://javadoc.io/doc/com.tngtech.archunit/archunit/0.23.1/com/tngtech/archunit/library/metrics/LakosMetrics.html

[^3_7]: https://accu.org/conf-docs/PDFs_Autumn_2019/John_Lakos_-_C++_Modules_and_Large-Scale_Development.pdf

[^3_8]: https://ptgmedia.pearsoncmg.com/images/9780201717068/samplepages/9780201717068_Sample.pdf

[^3_9]: https://github.com/nguyenchiemminhvu/large_scale_cpp_process_and_architecture_vol_1

[^3_10]: https://wiki.c2.com/?LargeScaleCppSoftwareDesign

[^3_11]: https://cs.brown.edu/courses/cs190/2008/documents/lakos/title_toc.pdf

[^3_12]: https://www.reddit.com/r/cpp/comments/e9uw2y/psa_john_lakos_update_to_large_scale_c_software/

[^3_13]: https://github.com/bloomberg/bde/wiki/physical-code-organization

[^3_14]: https://gist.github.com/brentarias/5816e41450dc8aa02f2fbfe066236308

[^3_15]: http://library.bagrintsev.me/CPP/Large-Scale C++ Software Design.pdf

[^3_16]: https://dzone.com/refcardz/designing-quality-software

[^3_17]: https://www.youtube.com/watch?v=K_fTl_hIEGY

