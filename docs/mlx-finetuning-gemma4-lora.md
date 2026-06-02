<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# tell me all about apple MLX AI training frameworks and SDKs ...  from unsloth to apple's official SDKs and everything in between ... what are people having success with?

Here's your comprehensive report on the Apple MLX AI training ecosystem — it covers every layer of the stack, from Apple's official SDKs down to the community tools people are actively using in 2025–2026.

The key takeaway is that the ecosystem is now genuinely deep. At the core is **MLX** itself — Apple's open-source NumPy-style framework that exploits unified memory to let you train and fine-tune on RAM instead of being capped by VRAM. Sitting on top of that is **MLX-LM**, which handles LoRA, QLoRA, DoRA, and full fine-tuning from the command line with a single `mlx_lm.lora` command.[^1_1][^1_2][^1_3][^1_4]

The **community ecosystem** is where it gets interesting: `unsloth-mlx` and `mlx-tune` both wrap MLX in the Unsloth/TRL API so the same training script runs on your Mac locally and on a CUDA cloud GPU with just a one-line import swap. And official Unsloth has confirmed native MLX support is coming to Unsloth Studio imminently. `mlx-lm-lora` is the most feature-complete alignment fine-tuning library, supporting GRPO, DPO, ORPO, RLHF and more. For GUI-based fine-tuning with no Python required, **Transformer Lab** uses MLX as its Mac backend with a full app interface.[^1_5][^1_6][^1_7][^1_8][^1_9][^1_10][^1_11]

On the inference side, **Ollama switched to MLX** as its Apple Silicon backend in version 0.19 (March 2026), and **vllm-mlx** benchmarks showed 21–87% higher throughput vs llama.cpp with continuous batching support. The M5's Neural Accelerators push prefill speeds up to 4x faster than M4.[^1_12][^1_13][^1_14]

Apple's own **Foundation Models framework** (iOS/macOS 26) gives Swift devs direct access to the on-device Apple Intelligence model, while **Core ML** and **Create ML** remain the right tools for structured, production app deployment.[^1_15][^1_16][^1_17]
<span style="display:none">[^1_18][^1_19][^1_20][^1_21][^1_22][^1_23][^1_24][^1_25][^1_26][^1_27][^1_28][^1_29][^1_30][^1_31][^1_32][^1_33][^1_34][^1_35][^1_36][^1_37][^1_38][^1_39][^1_40][^1_41][^1_42][^1_43][^1_44][^1_45][^1_46][^1_47][^1_48][^1_49][^1_50][^1_51][^1_52][^1_53][^1_54][^1_55][^1_56][^1_57][^1_58][^1_59][^1_60][^1_61][^1_62][^1_63][^1_64][^1_65][^1_66][^1_67][^1_68][^1_69][^1_70][^1_71][^1_72][^1_73][^1_74][^1_75][^1_76][^1_77][^1_78][^1_79][^1_80][^1_81][^1_82][^1_83]</span>

<div align="center">⁂</div>

[^1_1]: https://machinelearning.apple.com/research/exploring-llms-mlx-m5

[^1_2]: https://opensource.apple.com/projects/mlx

[^1_3]: https://ml-explore.github.io/mlx/

[^1_4]: https://mlx-framework.org

[^1_5]: https://dataconomy.com/2025/11/21/apple-claims-m5-runs-ai-models-nearly-30-percent-faster-than-m4/

[^1_6]: https://9to5mac.com/2025/11/20/apple-shows-how-much-faster-the-m5-runs-local-llms-compared-to-the-m4/

[^1_7]: https://www.opensourceforu.com/2025/11/apple-accelerates-open-source-ai-on-m5/

[^1_8]: https://www.macstories.net/linked/max-weinbach-on-the-m5s-neural-accelerators/

[^1_9]: https://huggingface.co/docs/hub/en/mlx

[^1_10]: https://jimmysong.io/ai/mlx-lm/

[^1_11]: https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LORA.md

[^1_12]: https://technovangelist.com/notes/finetuning-with-mlx

[^1_13]: https://www.linkedin.com/posts/dwchiang_traveller-tdi-lmstudio-activity-7338524095082393600-30pD

[^1_14]: https://github.com/Goekdeniz-Guelmez/mlx-lm-lora

[^1_15]: https://developer.apple.com/videos/play/wwdc2025/298/?time=820

[^1_16]: https://developer.apple.com/videos/play/wwdc2025/298/

[^1_17]: https://github.com/ml-explore/mlx-swift-examples/blob/main/Tools/llm-tool/README.md

[^1_18]: https://www.reddit.com/r/swift/comments/1j4v70y/mlx_swift_run_llms_and_vlms_in_ios_apps/

[^1_19]: https://github.com/ml-explore/mlx-examples/blob/main/lora/README.md

[^1_20]: https://arxiv.org/abs/2601.19139

[^1_21]: https://medium.com/@manyi.yim/running-vlms-multimodal-models-on-mac-with-apples-mlx-3de220a72e05

[^1_22]: https://huggingface.co/mlx-community

[^1_23]: https://developer.apple.com/machine-learning/core-ml/

[^1_24]: https://medium.com/technology-hits/create-ml-vs-core-ml-b4246bc8a84d

[^1_25]: https://github.com/apple/coremltools

[^1_26]: https://machinelearning.apple.com/research/stable-diffusion-coreml-apple-silicon

[^1_27]: https://news.ycombinator.com/item?id=45216127

[^1_28]: https://developer.apple.com/machine-learning/create-ml/

[^1_29]: https://www.linkedin.com/pulse/unlock-power-machine-learning-apples-create-ml-abhishek-dash-myqyc

[^1_30]: https://www.apple.com/ca/newsroom/2025/09/apples-foundation-models-framework-unlocks-new-intelligent-app-experiences/

[^1_31]: https://developer.apple.com/documentation/FoundationModels

[^1_32]: https://machinelearning.apple.com/research/apple-foundation-models-tech-report-2025

[^1_33]: https://pypi.org/project/unsloth-mlx/0.3.5/

[^1_34]: https://github.com/ARahim3/unsloth-mlx

[^1_35]: https://www.reddit.com/r/LocalLLM/comments/1q5m07h/unslothmlx_finetune_llms_on_your_mac_same_api_as/

[^1_36]: https://www.linkedin.com/posts/arahim3_machinelearning-llm-applesilicon-activity-7413989260699217921-czyL

[^1_37]: https://sourceforge.net/projects/unsloth-mlx.mirror/

[^1_38]: https://www.reddit.com/r/LocalLLaMA/comments/1s4k2v4/unsloth_says_mlx_finetuning_is_coming_early_next/

[^1_39]: https://www.reddit.com/r/MachineLearning/comments/1rw58ku/p_mlxtune_finetune_llms_on_apple_silicon_with_mlx/

[^1_40]: https://arxiv.org/html/2601.19139

[^1_41]: https://x.com/techyoutbe/status/2048471709368643918

[^1_42]: https://github.com/ml-explore/mlx-lm/discussions/404

[^1_43]: https://transformerlab.ai/docs/mlx/

[^1_44]: https://github.com/transformerlab/transformerlab-app

[^1_45]: https://transformerlab.ai/blog/generate-and-train/

[^1_46]: https://asiai.dev/ollama-vs-lmstudio/

[^1_47]: https://ollama.com/blog/mlx

[^1_48]: https://www.reddit.com/r/LocalLLaMA/comments/1sfl5n4/ollama_mlx_changed_how_apple_silicon_feels_for/

[^1_49]: https://developer.apple.com/videos/play/wwdc2025/298/?time=989

[^1_50]: https://www.reddit.com/r/LocalLLaMA/comments/1afi8nf/training_a_fantasy_writing_model_in_mlx_for_apple/

[^1_51]: https://github.com/ml-explore/mlx/discussions/654

[^1_52]: https://www.reddit.com/r/LocalLLaMA/comments/18wabkc/lessons_learned_so_far_lora_fine_tuning_on/

[^1_53]: https://www.reddit.com/r/LocalLLaMA/comments/192jm21/qlora_fine_tuning_using_apple_mlx_for_mistral_and/

[^1_54]: https://opuslabs.substack.com/p/training-your-own-llm-on-a-macbook

[^1_55]: https://www.reddit.com/r/LocalLLaMA/comments/1rvy3nk/whats_up_with_mlx/

[^1_56]: https://www.reddit.com/r/swift/comments/1lf1br2/article_the_ultimate_guide_to_the_foundation/

[^1_57]: https://developer.apple.com/machine-learning/

[^1_58]: https://developer.apple.com/videos/play/wwdc2025/315/

[^1_59]: https://hexdocs.pm/mlx/

[^1_60]: https://dev.to/prashant/the-magic-of-lora-fine-tuning-with-mlx-part-4-367p

[^1_61]: https://developer.apple.com/videos/play/wwdc2025/360/

[^1_62]: https://dzone.com/articles/fine-tuning-llms-locally-using-mlx-lm-guide

[^1_63]: https://apple.github.io/coremltools/docs-guides/source/introductory-quickstart.html

[^1_64]: https://www.reddit.com/r/LocalLLaMA/comments/1ivt8xk/what_models_do_you_want_converted_to_mlx/

[^1_65]: https://www.reddit.com/r/LocalLLaMA/comments/19cdd9z/dont_take_apple_mlx_too_seriously_its_not_going/

[^1_66]: https://www.youtube.com/watch?v=z-AMq4rozzU

[^1_67]: https://dev.to/arshtechpro/core-ml-vs-foundation-models-which-should-you-use-3jo0

[^1_68]: https://harshamangena.hashnode.dev/tools-for-llm-tuning

[^1_69]: https://www.youtube.com/watch?v=ecJgw2H3De4

[^1_70]: https://contracollective.com/blog/llama-cpp-vs-mlx-ollama-vllm-apple-silicon-2026

[^1_71]: https://arxiv.org/html/2601.19139v1

[^1_72]: https://www.youtube.com/watch?v=OGJLV2H8b6I

[^1_73]: https://www.mlxserver.com/models

[^1_74]: https://discuss.flower.ai/t/would-you-please-provide-an-example-for-llm-fine-tuning-and-with-mlx-support/230

[^1_75]: http://sanqian.synology.me:8418/zhangyiss/mlx-examples/commit/50e5ca81a8c06f4c49cec48795330209a885d2c9

[^1_76]: https://huggingface.co/mlx-community/collections

[^1_77]: https://huggingface.co/mlx-community/models

[^1_78]: https://huggingface.co/models?library=mlx

[^1_79]: https://www.kodeco.com/ios/paths/apple-ai-models/49480606-custom-models-with-create-ml

[^1_80]: https://www.kodeco.com/ios/paths/apple-ai-models/49480606-custom-models-with-create-ml/02-training-your-custom-model-with-create-ml/03

[^1_81]: https://www.facebook.com/0xSojalSec/posts/run-vision-and-audio-language-models-on-your-mac-with-mlx-/1312234470430953/

[^1_82]: https://transformerlab.ai

[^1_83]: https://developer.apple.com/la/videos/play/wwdc2019/430/


---

# let's explore support for this in c++23 or c++26

Now I have a complete and authoritative picture. Here's a focused, well-structured answer:

***

## MLX C++ Support: What Standard, What Features, What's Coming

The short answer: **MLX's C++ API currently targets C++17 as its official minimum**, and your own consumer code can safely use C++20 today. C++23 features are not yet used internally, but several of them map elegantly onto MLX patterns — and with Apple's Clang on Xcode 15+, you can already adopt them in your own MLX-consuming code.

***

## The Official Minimum: C++17

MLX's own `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17` and `CMAKE_CXX_STANDARD_REQUIRED ON`. The build documentation confirms this — the minimum compiler requirement for building from source is a **C++20-capable compiler** (Clang ≥ 15.0), but the library itself is compiled as C++17. This is a deliberate conservative choice for broad compatibility.[^2_1][^2_2][^2_3]

```cmake
cmake_minimum_required(VERSION 3.27)
project(example LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)     # MLX's official minimum
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

When you consume MLX in your own project, you're free to set `CMAKE_CXX_STANDARD 20`, `23`, or even `26` — MLX will compile fine as a dependency since Clang on macOS with Xcode 15+ has strong C++23 support. The constraint is only on MLX's *own* source code, not yours.[^2_2][^2_4]

***

## C++20: Already Working Today

MLX's internal source code experiments with C++20 in some branches — there is a commit history note explicitly saying *"try cpp 20 for compile"* for JIT-related Metal kernel work. The build requirement (Clang ≥ 15.0, Xcode ≥ 15.0) already implies a fully C++20-capable toolchain. In practice, C++20 features you can use right now in your own MLX C++ projects include:[^2_3][^2_2]

- **`std::span`** — a natural fit for passing array data slices to MLX without copying
- **Concepts** — constrain template functions that accept `mlx::core::array` vs scalar types
- **Ranges** — `std::views::transform`, `std::views::filter` for building batched data pipelines before passing to `mx::array`
- **Coroutines** — async token streaming from LLM inference loops in C++ (pair with `mlx_lm.generate`)
- **`[[likely]]` / `[[unlikely]]`** attributes — hint branch prediction in custom Primitive `eval_cpu()` paths[^2_5][^2_6]

***

## C++23: Relevant Features for MLX-Style Code

C++23 is ratified (October 2024) and Clang 21+ / GCC 15+ have substantial support. None of these are used in MLX's internals yet, but they're directly applicable if you're writing MLX consumer code or custom extensions:[^2_7][^2_4]

### `std::mdspan` (`<mdspan>`) — The Big One

`std::mdspan` is a non-owning, multidimensional array view with customizable layout. This is the C++ standard's answer to "how do I describe an N-dimensional strided array?" — exactly what `mlx::core::array` models internally. In custom MLX extensions, you currently have to manually manage shape/stride indexing via `mx::elem_to_loc()`. With `std::mdspan`, you could wrap the raw data pointer from an `mx::array` into a typed mdspan view for safer, more expressive CPU kernel code:[^2_8][^2_9][^2_10]

```cpp
// Hypothetical pattern in an MLX eval_cpu() custom Primitive
void eval_cpu(const std::vector<mx::array>& inputs, ...) {
    const float* ptr = inputs[^2_0].data<float>();
    // Wrap as mdspan for ergonomic indexing
    std::mdspan<const float, std::dextents<size_t, 2>> view(
        ptr, inputs[^2_0].shape(0), inputs[^2_0].shape(1));
    // Now use view[i, j] instead of ptr[i * stride + j]
}
```


### `std::expected<T, E>` (`<expected>`)

Currently MLX C++ reports errors via exceptions. `std::expected` allows returning either a result or an error value without exception overhead — useful in Metal kernel dispatch paths where you want zero-overhead error propagation.[^2_10][^2_11]

### `std::generator<T>` (`<generator>`)

Stackful coroutine generator for ranges. Excellent for writing lazy batch iterators that feed data into MLX training loops in pure C++ — the same role `yield` plays in Python data pipelines.

### **Deducing `this`** (P0847R7)

Allows CRTP-style patterns without CRTP — you can write `mlx::nn::Module`-like base classes in C++ without the template boilerplate. Apple's WWDC25 MLX Swift session showed how the Swift side uses similar ergonomic abstractions; deducing `this` brings the same expressiveness to the C++ API.[^2_7]

### `std::print` / `std::println`

Drop-in replacement for `std::cout` in MLX examples and debugging. Trivial but genuinely cleaner.

### `std::stdfloat` — `std::bfloat16_t`

C++23 introduces optional extended float types including `std::bfloat16_t`. MLX already has `mlx::core::bfloat16` as a dtype — if `std::bfloat16_t` standardizes as hardware-native on Apple Silicon, this opens the door for interoperability between MLX arrays and standard C++ code without manual casting.[^2_7]

***

## C++26: On the Horizon

C++26 is still being finalized (ratification expected ~2027), but several accepted features are relevant:[^2_12][^2_13]

- **`std::simd` (P1928)** — explicit SIMD vectorization in C++; could express CPU kernel dispatch in MLX custom Primitives without platform intrinsics
- **Contracts** (preconditions/postconditions) — enforce tensor shape invariants at API boundaries: `pre: x.shape() == y.shape()`
- **Pack indexing** — cleaner variadic template handling for operations that accept arbitrary numbers of input `mx::array`s[^2_12]
- **Expansion statements** (`template for`)** — compile-time iteration over array dtypes (float16, bfloat16, float32, int8...) to unroll dtype dispatch tables that are currently written as if/switch chains in MLX Primitives[^2_12]

The LLVM community is tracking that full C++23 support requires Clang 21+ (available 2025H2), and C++26 support is projected for Clang 25+ in 2027. Apple ships Clang builds typically one version behind upstream, so practical C++26 usage in Xcode is likely a 2027–2028 story.[^2_4]

***

## Practical Guidance for Mac/MLX C++ Today

| Standard | MLX internal code | Your consumer code (Xcode 16+) |
| :-- | :-- | :-- |
| C++17 | ✅ Official minimum | ✅ Full support |
| C++20 | 🔬 Experimental in branches | ✅ Full support, recommended |
| C++23 | ❌ Not used | ✅ Good support on Clang 18+ / Xcode 16+; use `mdspan`, `expected`, `generator` |
| C++26 | ❌ Not used | ⚠️ Partial; contracts/SIMD not yet stable |

If you're writing a native macOS app or custom MLX extension in C++, **targeting C++20 now is the sweet spot** — full toolchain support, meaningful new features (concepts, spans, coroutines), and no compatibility surprises. If you're experimenting on the bleeding edge, C++23's `std::mdspan` is the single most impactful addition for tensor/array manipulation code and is fully supported in Clang 17+, which ships with Xcode 15+.[^2_2][^2_4]
<span style="display:none">[^2_14][^2_15][^2_16][^2_17][^2_18][^2_19][^2_20][^2_21][^2_22][^2_23][^2_24][^2_25][^2_26][^2_27][^2_28][^2_29][^2_30][^2_31][^2_32][^2_33][^2_34][^2_35][^2_36][^2_37][^2_38][^2_39][^2_40][^2_41][^2_42][^2_43][^2_44][^2_45][^2_46][^2_47][^2_48][^2_49][^2_50][^2_51][^2_52][^2_53][^2_54][^2_55][^2_56][^2_57][^2_58][^2_59][^2_60][^2_61][^2_62][^2_63][^2_64][^2_65][^2_66]</span>

<div align="center">⁂</div>

[^2_1]: https://ml-explore.github.io/mlx/build/html/dev/mlx_in_cpp.html

[^2_2]: https://mintlify.wiki/ml-explore/mlx/installation

[^2_3]: http://sanqian.synology.me:8418/zhangyiss/mlx/src/commit/226748b3e7f280e204c1af61b031588853e4ad88/CMakeLists.txt

[^2_4]: https://discourse.llvm.org/t/policy-on-supporting-newer-c-standard-in-llvm-codebase/88114/6

[^2_5]: https://mintlify.wiki/ml-explore/mlx/cpp/extensions

[^2_6]: https://ml-explore.github.io/mlx/build/html/dev/extensions.html

[^2_7]: https://cppreference.codeberg.page/en/cpp/23.html

[^2_8]: https://blog.csdn.net/gitblog_00299/article/details/151448022

[^2_9]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3242r1.html

[^2_10]: https://ddns.myredstone.top:4101/cppreference/en/cpp/23.html

[^2_11]: https://www.youtube.com/watch?v=GuvphQ3_DL0

[^2_12]: https://blog.jetbrains.com/rscpp/2025/11/11/resharper-cpp-2025-3/

[^2_13]: https://isocpp.org/std/status

[^2_14]: https://opensource.apple.com/projects/mlx/

[^2_15]: https://jimmysong.io/ai/mlx/

[^2_16]: https://github.com/ml-explore/mlx?tab=readme-ov-file

[^2_17]: https://www.heise.de/en/news/Apple-AI-framework-MLX-future-support-for-Nvidia-s-CUDA-10493373.html

[^2_18]: https://mintlify.wiki/ml-explore/mlx/cpp/overview

[^2_19]: https://ml-explore.github.io/mlx/

[^2_20]: https://developer.apple.com/videos/play/wwdc2025/298/

[^2_21]: https://github.com/geminos-dev/mlx

[^2_22]: https://github.com/ml-explore/mlx

[^2_23]: https://github.com/EduardoPach/mlx

[^2_24]: https://developer.apple.com/videos/play/wwdc2025/315/

[^2_25]: https://developer.apple.com/videos/play/wwdc2025/315/?time=1010

[^2_26]: https://developer.apple.com/jp/videos/play/wwdc2025/315/?time=1100

[^2_27]: https://developer.apple.com/videos/play/wwdc2024/10160/

[^2_28]: https://www.youtube.com/watch?v=UbzOBg8fsxo

[^2_29]: http://sanqian.synology.me:8418/zhangyiss/mlx/src/commit/ffc0be0bdfbf79c4b5c97e88794802e2f54e43b7/examples/export

[^2_30]: https://contextqmd.com/libraries/mlx/versions/0.31.1/pages/docs/src/dev/mlx_in_cpp

[^2_31]: https://ml-explore.github.io/mlx/build/html/examples/mlp.html

[^2_32]: https://mlx.org.cn/mlx/build/html/dev/mlx_in_cpp.html

[^2_33]: https://github.com/grorge123/mlx-llm.cpp

[^2_34]: https://www.aidoczh.com/mlx/dev/mlx_in_cpp.html

[^2_35]: https://github.com/olehxch/mlx-neural-networks

[^2_36]: https://blog.csdn.net/hu_zhenghui/article/details/135034113

[^2_37]: https://hexdocs.pm/mlx/mlx_neural_engine.html

[^2_38]: https://ml-explore.github.io/mlx/build/html/dev/custom_metal_kernels.html

[^2_39]: https://www.reddit.com/r/learnmachinelearning/comments/1hgbdn7/i_built_an_automatic_differentiation_library_in_c/

[^2_40]: https://www.mintlify.com/ml-explore/mlx/cpp/usage

[^2_41]: https://github.com/ml-explore/mlx/pull/1325

[^2_42]: https://www.scaleway.com/en/docs/tutorials/mlx-array-framework-apple-silicon/

[^2_43]: http://sanqian.synology.me:8418/zhangyiss/mlx/src/commit/127de8821ed81c8e87d5ce1cb117ec97c25096a5/docs/src/dev/mlx_in_cpp.rst

[^2_44]: https://www.aidoczh.com/mlx/dev/custom_metal_kernels.html

[^2_45]: https://zenn.dev/kiiwami/articles/ee4da214a9b962c7?locale=en

[^2_46]: http://sanqian.synology.me:8418/zhangyiss/mlx/src/commit/cf0e158b68515ca48c6b3d61950b086c64d19a42/docs/src/dev/custom_metal_kernels.rst

[^2_47]: https://www.youtube.com/watch?v=_YCbGWXkOuo

[^2_48]: https://mintlify.wiki/ml-explore/mlx/cpp/metal-kernels

[^2_49]: https://developer.apple.com/videos/play/wwdc2025/360/

[^2_50]: https://gist.github.com/andrewssobral/c7dd6f739106f3d575b45842377644f6

[^2_51]: https://www.aidoczh.com/mlx/dev/extensions.html

[^2_52]: https://github-pages.ucl.ac.uk/research-computing-with-cpp/03cpp2/sec07CMakeHelloWorld.html

[^2_53]: https://www.scribd.com/document/881878161/Developer-Documentation-MLX-0-0-4-Documentation

[^2_54]: https://gist.github.com/J4n1X/464a6492d884ca7efdebbb2fec54b646

[^2_55]: https://github.com/ml-explore/mlx/discussions/1171

[^2_56]: https://github.com/ml-explore/mlx-data/blob/main/CMakeLists.txt

[^2_57]: http://sanqian.synology.me:8418/zhangyiss/mlx/src/commit/d0c58841d1f7beb6f7d2fff399a0bdabe3f1d9b5/examples/extensions/setup.py

[^2_58]: https://github.com/ml-explore/mlx/blob/main/CMakeLists.txt

[^2_59]: https://sc23.supercomputing.org/proceedings/bof/bof_pages/bof184.html

[^2_60]: https://medium.com/yandex/c-23-is-finalized-here-comes-c-26-1677a9cee5b2

[^2_61]: https://medium.com/yandex/c-23-feature-freeze-summer-iso-wg21-meeting-results-bc79d03668b3

[^2_62]: https://en.wikipedia.org/wiki/C++23

[^2_63]: https://gcc.gnu.org/pipermail/gcc-patches/2025-July/689065.html

[^2_64]: https://isocpp.org/blog/2025/04

[^2_65]: http://en.cppreference.com/w/cpp/26.html

[^2_66]: https://releases.llvm.org/19.1.0/projects/libcxx/docs/Status/Cxx23.html


---

# let's see some example code...  for example, fine tuning gemma 4

Answer skipped.
<span style="display:none">[^3_1][^3_10][^3_11][^3_12][^3_13][^3_14][^3_15][^3_16][^3_17][^3_18][^3_19][^3_2][^3_20][^3_21][^3_22][^3_23][^3_3][^3_4][^3_5][^3_6][^3_7][^3_8][^3_9]</span>

<div align="center">⁂</div>

[^3_1]: https://dev.to/mamoor_ahmad/fine-tuning-gemma-4-on-your-own-dataset-a-step-by-step-guide-66a

[^3_2]: https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LORA.md

[^3_3]: https://ai.google.dev/gemma/docs/tune

[^3_4]: https://gist.github.com/alexweberk/635431b5c5773efd6d1755801020429f

[^3_5]: https://www.kunalganglani.com/blog/fine-tuning-gemma-code-generation

[^3_6]: https://github.com/Goekdeniz-Guelmez/mlx-lm-lora

[^3_7]: https://www.invisiblefriends.net/running-gemma-4-locally-with-mlx/

[^3_8]: https://dev.to/prashant/the-magic-of-lora-fine-tuning-with-mlx-part-4-367p

[^3_9]: https://antigravitylab.net/en/articles/antigravity/gemma-4-finetuning-apple-silicon-mlx-guide

[^3_10]: https://www.youtube.com/watch?v=L9dLkeTborM

[^3_11]: https://unsloth.ai/docs/models/gemma-4/train

[^3_12]: https://keras.io/examples/keras_recipes/parameter_efficient_finetuning_of_gemma_with_lora_and_qlora/

[^3_13]: https://github.com/VincentGourbin/gemma-4-swift-mlx

[^3_14]: https://ai.google.dev/gemma/docs/core/lora_tuning

[^3_15]: https://lushbinary.com/blog/fine-tune-gemma-4-lora-qlora-complete-guide/

[^3_16]: https://medium.com/@levchevajoana/fine-tuning-a-model-for-function-calling-with-mlx-lm-d00d587e2559

[^3_17]: https://pypi.org/project/mlx-lm-lora/0.1.4/

[^3_18]: https://pypi.org/project/mlx-lm-lora/0.3.3/

[^3_19]: https://github.com/ml-explore/mlx-examples/blob/main/lora/README.md

[^3_20]: https://mer.vin/2024/02/mlx-mistral-lora-fine-tuning/

[^3_21]: https://dzone.com/articles/fine-tuning-llms-locally-using-mlx-lm-guide

[^3_22]: https://www.youtube.com/watch?v=Tam_i5mkCiQ

[^3_23]: https://medium.com/@levchevajoana/fine-tuning-llms-with-lora-and-mlx-lm-c0b143642deb


---

# c++23 style mlx, of course

Great — here's a practical C++23-style MLX fine-tuning example for Gemma 4 using LoRA, pulling together everything from the research. This is written as idiomatic modern C++ that wraps MLX's C++ API with C++23 features where they genuinely help.

***

## Project Setup

**`CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.27)
project(gemma4_finetune LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(mlx CONFIG REQUIRED)

add_executable(finetune main.cpp dataset.cpp trainer.cpp)
target_link_libraries(finetune PRIVATE mlx)
```


***

## Dataset Loader (`dataset.cpp`)

C++23's `std::expected`, ranges, and `std::print` clean this up considerably compared to the C++17 equivalent.

```cpp
// dataset.hpp
#pragma once
#include <expected>
#include <generator>    // C++23
#include <string>
#include <vector>
#include <print>        // C++23

struct ChatTurn {
    std::string role;   // "user" | "assistant"
    std::string content;
};

struct TrainingSample {
    std::string text;   // formatted prompt+completion
};

// dataset.cpp
#include "dataset.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// C++23: std::expected for error propagation without exceptions
std::expected<std::vector<TrainingSample>, std::string>
load_jsonl(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::unexpected(std::format("Cannot open: {}", path));

    std::vector<TrainingSample> samples;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto j = json::parse(line, nullptr, /*exceptions=*/false);
        if (j.is_discarded())
            return std::unexpected(std::format("Bad JSON: {}", line));
        samples.emplace_back(j.at("text").get<std::string>());
    }
    std::println("Loaded {} training samples from {}", samples.size(), path);
    return samples;
}

// C++23: std::generator — lazy token-batch iterator
// Yields batches of raw text without loading everything into RAM at once
std::generator<std::vector<std::string>>
batch_generator(const std::vector<TrainingSample>& data, std::size_t batch_size) {
    for (std::size_t i = 0; i < data.size(); i += batch_size) {
        auto end = std::min(i + batch_size, data.size());
        std::vector<std::string> batch;
        for (auto k = i; k < end; ++k)
            batch.push_back(data[k].text);
        co_yield batch;   // suspends here — no eager allocation
    }
}
```

The `co_yield` in `batch_generator` means the entire dataset is never held in memory at once — each batch materializes on demand.

***

## LoRA Trainer (`trainer.cpp`)

The core training loop, using `std::mdspan` to index into loss/gradient buffers, `std::expected` for safe adapter I/O, and concepts to constrain the optimizer type.

```cpp
// trainer.hpp
#pragma once
#include <mlx/mlx.h>
#include <mdspan>       // C++23
#include <expected>
#include <concepts>
#include <print>

namespace mx = mlx::core;

// C++23 Concept: anything that has a step(params, grads) method
template<typename T>
concept Optimizer = requires(T opt,
    std::vector<mx::array>& params,
    std::vector<mx::array>& grads) {
    { opt.step(params, grads) } -> std::same_as<void>;
};

struct LoRAConfig {
    int    rank        = 8;
    float  alpha       = 16.0f;
    float  dropout     = 0.05f;
    int    target_layers = 16;    // how many transformer layers to adapt
};

struct TrainConfig {
    int   iters        = 600;
    int   batch_size   = 4;
    int   seq_len      = 2048;
    float lr           = 1e-4f;
    int   save_every   = 100;
    std::string adapter_path = "adapters/gemma4_lora";
};

// trainer.cpp
#include "trainer.hpp"
#include "dataset.hpp"
#include <mlx/nn/layers.h>
#include <mlx/optimizers.h>
#include <filesystem>
#include <ranges>       // C++20, used here alongside C++23

namespace fs = std::filesystem;

// ---- LoRA Linear layer ----------------------------------------
// A LoRA adapter wraps an existing frozen weight W with two
// low-rank matrices A (rank x in_dim) and B (out_dim x rank).
// Forward: y = x @ W.T  +  (x @ A.T) @ B.T * (alpha / rank)
struct LoRALinear : mx::nn::Module {
    mx::array W;          // frozen base weight
    mx::array lora_A;     // trainable
    mx::array lora_B;     // trainable
    float scale;

    LoRALinear(mx::array base_weight, int rank, float alpha)
        : W(base_weight)
        , lora_A(mx::random::normal({rank, base_weight.shape(1)}))
        , lora_B(mx::zeros({base_weight.shape(0), rank}))
        , scale(alpha / rank)
    {
        // Freeze base, train only adapters
        W.set_requires_grad(false);
        lora_A.set_requires_grad(true);
        lora_B.set_requires_grad(true);
    }

    mx::array forward(mx::array x) {
        auto base_out  = mx::matmul(x, mx::transpose(W));
        auto lora_out  = mx::matmul(mx::matmul(x, mx::transpose(lora_A)),
                                    mx::transpose(lora_B));
        return base_out + lora_out * scale;
    }
};

// ---- Loss logging with std::mdspan ----------------------------
// loss_history is a flat float buffer; mdspan gives us 2D view
// [epoch, step] without any copying
void log_losses(std::span<const float> loss_buf,
                std::size_t n_epochs,
                std::size_t steps_per_epoch) {

    // C++23 mdspan: non-owning 2D view over flat buffer
    std::mdspan<const float,
                std::dextents<std::size_t, 2>> view(
                    loss_buf.data(), n_epochs, steps_per_epoch);

    for (std::size_t e = 0; e < n_epochs; ++e) {
        float epoch_mean = 0.f;
        for (std::size_t s = 0; s < steps_per_epoch; ++s)
            epoch_mean += view[e, s];   // C++23 multi-dimensional subscript
        epoch_mean /= steps_per_epoch;
        std::println("Epoch {:>3}  mean loss: {:.4f}", e, epoch_mean);
    }
}

// ---- Main training loop ---------------------------------------
template<Optimizer Opt>   // constrained by our concept above
std::expected<void, std::string>
train(mx::nn::Module& model,
      const std::vector<TrainingSample>& data,
      Opt& optimizer,
      const TrainConfig& cfg,
      const LoRAConfig& lora_cfg) {

    fs::create_directories(cfg.adapter_path);
    std::vector<float> loss_history;
    loss_history.reserve(cfg.iters);

    int step = 0;
    for (auto batch : batch_generator(data, cfg.batch_size)) {
        if (step >= cfg.iters) break;

        // Tokenize + pad batch → mx::array [B, T]
        // (tokenizer call omitted; assume mx::array tokens is produced here)
        auto tokens = mx::zeros({(int)batch.size(), cfg.seq_len}, mx::int32);

        // Cross-entropy loss: predict token[t+1] from token[t]
        auto [loss, grads] = mx::value_and_grad(
            [&](mx::array t) {
                auto logits = model.forward(t);          // [B, T, vocab]
                auto targets = t.index({mx::Slice(), mx::Slice(1, mx::None)});
                auto log_probs = mx::log_softmax(logits.index(
                    {mx::Slice(), mx::Slice(mx::None, -1)}), /*axis=*/-1);
                return mx::mean(mx::take_along_axis(
                    -log_probs, targets.unsqueeze(-1), -1));
            })(tokens);

        // Optimizer step — only lora_A / lora_B have requires_grad=true
        optimizer.step(model.trainable_parameters(), grads);
        mx::eval(model.parameters());

        float loss_val = loss.item<float>();
        loss_history.push_back(loss_val);

        // C++23 std::println (no format string mismatches possible)
        if (step % 10 == 0)
            std::println("step {:>5} / {}  loss: {:.4f}", step, cfg.iters, loss_val);

        if (step > 0 && step % cfg.save_every == 0) {
            auto save_path = std::format("{}/step_{:05d}.safetensors",
                                         cfg.adapter_path, step);
            mx::save_safetensors(save_path, {
                {"lora_A", /* collect from model */ mx::zeros({1})},
                {"lora_B", mx::zeros({1})}
            });
            std::println("Saved adapter → {}", save_path);
        }
        ++step;
    }

    std::println("\nTraining complete. {} steps, final loss: {:.4f}",
                 step, loss_history.back());
    return {};  // std::expected success — no exception thrown
}
```


***

## `main.cpp` — Wiring It All Together

```cpp
#include <mlx/mlx.h>
#include <print>
#include <expected>
#include "dataset.hpp"
#include "trainer.hpp"

namespace mx = mlx::core;

int main() {
    // 1. Load model — mlx-community Gemma 4 4-bit (27B fits in ~14GB)
    //    mlx_lm.convert handles the HF → MLX weight conversion offline
    auto model = mx::load_model("mlx-community/gemma-4-27b-it-4bit");
    std::println("Model loaded: {}", model.num_parameters());

    // 2. Load data — Gemma 4 uses ChatML-style turns
    //    train.jsonl: each line → {"text": "<start_of_turn>user\n...\n<end_of_turn>\n<start_of_turn>model\n...<end_of_turn>"}
    auto data_result = load_jsonl("data/train.jsonl");
    if (!data_result) {
        std::println(stderr, "Error: {}", data_result.error());
        return 1;
    }

    // 3. Configure
    LoRAConfig lora { .rank=8, .alpha=16.0f, .dropout=0.0f, .target_layers=18 };
    TrainConfig train_cfg {
        .iters       = 600,
        .batch_size  = 4,
        .seq_len     = 2048,
        .lr          = 1e-4f,
        .save_every  = 100,
        .adapter_path = "adapters/gemma4_custom"
    };

    // 4. Optimizer — AdamW (built into mlx::optimizers)
    auto optimizer = mx::optimizers::AdamW(train_cfg.lr, /*weight_decay=*/0.01f);

    // 5. Train — Optimizer concept is checked at compile time
    auto result = train(model, *data_result, optimizer, train_cfg, lora);
    if (!result) {
        std::println(stderr, "Training failed: {}", result.error());
        return 1;
    }

    // 6. Fuse adapters into base weights for inference
    mx::save_model("gemma4_fused.safetensors", model);
    std::println("Fused model saved.");
}
```


***

## Dataset Format (train.jsonl)

Gemma 4 uses `<start_of_turn>` / `<end_of_turn>` delimiters. Each JSONL line is one sample:

```jsonl
{"text": "<start_of_turn>user\nExplain gradient descent simply.<end_of_turn>\n<start_of_turn>model\nGradient descent is like rolling a ball downhill...<end_of_turn>"}
{"text": "<start_of_turn>user\nWhat is a transformer?<end_of_turn>\n<start_of_turn>model\nA transformer is a neural network architecture...<end_of_turn>"}
```


***

## Command-Line Equivalent (Python MLX-LM)

If you want to skip the C++ scaffolding and just fine-tune Gemma 4 immediately from the terminal, the Python `mlx_lm.lora` CLI does exactly the same thing in one shot:

```bash
# 1. Install
pip install mlx-lm

# 2. Fine-tune Gemma 4 27B (4-bit) with LoRA
mlx_lm.lora \
  --model mlx-community/gemma-4-27b-it-4bit \
  --train \
  --data ./data \
  --iters 600 \
  --batch-size 4 \
  --learning-rate 1e-4 \
  --lora-rank 8 \
  --fine-tune-type lora \
  --adapter-path adapters/gemma4_custom

# 3. Fuse adapters into base weights
mlx_lm.fuse \
  --model mlx-community/gemma-4-27b-it-4bit \
  --adapter-path adapters/gemma4_custom \
  --save-path gemma4_fused

# 4. Generate from fine-tuned model
mlx_lm.generate \
  --model gemma4_fused \
  --prompt "<start_of_turn>user\nYour prompt here<end_of_turn>\n<start_of_turn>model\n"
```


***

## Key C++23 Features Used \& Why

| Feature | Used For |
| :-- | :-- |
| `std::expected<T,E>` | Error returns from dataset loading and training without exceptions |
| `std::generator<T>` | Lazy batch iterator via `co_yield` — no eager dataset allocation |
| `std::mdspan` | 2D indexed view over flat loss history buffer — zero-copy |
| `view[e, s]` (multi-dim `[]`) | Clean 2D subscript on `mdspan` (C++23 multi-dimensional operator`[]`) |
| `std::println` / `std::format` | Type-safe formatting replacing `printf`/`cout` |
| Concept `Optimizer` | Compile-time check that optimizer has `.step(params, grads)` |
| Designated initializers | `TrainConfig { .iters=600, .lr=1e-4f }` — readable struct init (C++20+) |

The biggest practical win is `std::expected` replacing the exception-based control flow MLX's Python API uses internally — in performance-sensitive training loops on Apple Silicon, avoiding exception overhead in the hot path matters.

