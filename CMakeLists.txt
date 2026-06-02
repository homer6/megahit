cmake_minimum_required(VERSION 3.5...4.1)
project(megahit)
set(CMAKE_VERBOSE_MAKEFILE ON)

# C++23 (this fork's target; AppleClang 15 emits -std=c++2b, clang 16+/Homebrew clang emits -std=c++23).
# Vendored deps were patched for C++23: parallel_hashmap (std::result_of -> std::invoke_result; <cstdlib> for
# std::abort) and idba (dropped removed std::unary_function); see those files' [megahit C++23 patch] notes.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COVERAGE "Generate coverage report" OFF)
option(STATIC_BUILD "Build static executation" OFF)
option(SANITIZER "Enable sanitizers" OFF)
option(TSAN "Enable thread sanitizers" OFF)

include_directories(src)
FILE(GLOB_RECURSE ASMBL_SOURCE "src/assembly/*.cpp")
FILE(GLOB_RECURSE LCASM_SOURCE "src/localasm/*.cpp")
FILE(GLOB_RECURSE IDBA_SOURCE "src/idba/*.cpp")
FILE(GLOB_RECURSE SDBG_SOURCE "src/sdbg/*.cpp")
FILE(GLOB_RECURSE CX1_SOURCE "src/sorting/*.cpp")
FILE(GLOB_RECURSE SEQ_SOURCE "src/sequence/*.cpp")
FILE(GLOB_RECURSE TOOLKIT_SOURCE "src/tools/*.cpp")

LIST(APPEND OTHER_SOURCE
        src/main.cpp
        src/main_assemble.cpp
        src/main_buildlib.cpp
        src/main_iterate.cpp
        src/main_local_assemble.cpp
        src/main_sdbg_build.cpp
        src/utils/options_description.cpp
        )

if (STATIC_BUILD)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif (STATIC_BUILD)

find_package(ZLIB REQUIRED)
find_package(OpenMP REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DXXH_INLINE_ALL -ftemplate-depth=3000")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprefetch-loop-arrays -funroll-loops")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__XROOT__='\"${CMAKE_SOURCE_DIR}/src\"'")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__XFILE__='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ZLIB_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O1 -D_LIBCPP_DEBUG -D_GLIBCXX_DEBUG")

if (COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 --coverage")
    set(COV_PY "coverage run")
endif (COVERAGE)

if (SANITIZER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=leak -fsanitize=undefined")
endif (SANITIZER)

if (TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
endif (TSAN)


message(STATUS "Build type: ${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS}")

add_executable(megahit_core ${OTHER_SOURCE} ${ASMBL_SOURCE} ${IDBA_SOURCE} ${SDBG_SOURCE} ${LCASM_SOURCE} ${SEQ_SOURCE}
        ${CX1_SOURCE} ${TOOLKIT_SOURCE})
add_executable(megahit_core_popcnt ${OTHER_SOURCE} ${ASMBL_SOURCE} ${IDBA_SOURCE} ${SDBG_SOURCE} ${LCASM_SOURCE} ${SEQ_SOURCE}
        ${CX1_SOURCE} ${TOOLKIT_SOURCE})
add_executable(megahit_core_no_hw_accel ${OTHER_SOURCE} ${ASMBL_SOURCE} ${IDBA_SOURCE} ${SDBG_SOURCE} ${LCASM_SOURCE}
        ${SEQ_SOURCE} ${CX1_SOURCE} ${TOOLKIT_SOURCE})

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64)|(amd64)|(AMD64)")
    set_target_properties(megahit_core PROPERTIES COMPILE_FLAGS "-mbmi2 -DUSE_BMI2 -mpopcnt")
    set_target_properties(megahit_core_popcnt PROPERTIES COMPILE_FLAGS "-mpopcnt")
endif ()


if (STATIC_BUILD)
    # TODO dirty
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -static")
    set_target_properties(megahit_core megahit_core_popcnt megahit_core_no_hw_accel PROPERTIES LINK_SEARCH_START_STATIC ON)
    set_target_properties(megahit_core megahit_core_popcnt megahit_core_no_hw_accel PROPERTIES LINK_SEARCH_END_STATIC ON)
endif (STATIC_BUILD)

target_link_libraries(megahit_core ${ZLIB_LIBRARIES})
target_link_libraries(megahit_core_popcnt ${ZLIB_LIBRARIES})
target_link_libraries(megahit_core_no_hw_accel ${ZLIB_LIBRARIES})

# The Python driver (src/megahit) and the `simple_test` end-to-end suite that ran through it were REMOVED in
# the C++23 / Seastar conversion — all orchestration is being rebuilt as a Seastar service. The pipeline/
# per-stage-command contract the new driver must replicate is preserved in docs/legacy-driver-spec.md; the
# service architecture in docs/megahit-as-a-service.md. `megahit_core*` remain the assembly multi-tool;
# `megahit_toolkit` (a C++ symlink) keeps the toolkit subcommands. A new end-to-end test harness lands with
# the Seastar driver.
add_custom_target(megahit_toolkit ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink megahit_core_no_hw_accel megahit_toolkit)
add_dependencies(megahit_toolkit megahit_core_no_hw_accel)

install(TARGETS megahit_core megahit_core_popcnt megahit_core_no_hw_accel DESTINATION bin)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/megahit_toolkit DESTINATION bin)

# ---- Seastar assembly service (Phase 0 of the rewrite; LINUX ONLY — Seastar has no macOS reactor, see
#      docs/seastar-guide.md §0 and docs/megahit-as-a-service.md). OFF by default so the legacy build on any
#      host is unaffected; enable on a Linux box / container with -DBUILD_SEASTAR_SERVER=ON.
option(BUILD_SEASTAR_SERVER "Build the Seastar assembly service (Linux only)" OFF)
if (BUILD_SEASTAR_SERVER)
    find_package(Seastar REQUIRED)   # provide Seastar's build dir via -DSeastar_DIR=... or pkg-config
    # The service reuses every megahit_core stage TU EXCEPT src/main.cpp (it supplies its own entry point).
    # (Transitional: factor these into a shared stage object-library in a later phase to avoid the duplicated
    # list; kept explicit for now to minimize churn to the legacy targets.)
    set(MEGAHIT_STAGE_SOURCE
            src/main_assemble.cpp src/main_buildlib.cpp src/main_iterate.cpp
            src/main_local_assemble.cpp src/main_sdbg_build.cpp src/utils/options_description.cpp
            ${ASMBL_SOURCE} ${IDBA_SOURCE} ${SDBG_SOURCE} ${LCASM_SOURCE} ${SEQ_SOURCE}
            ${CX1_SOURCE} ${TOOLKIT_SOURCE})
    add_executable(megahit_server
            src/server/main.cc
            src/server/cluster.cc
            src/server/assembly_engine.cc
            ${MEGAHIT_STAGE_SOURCE})
    target_include_directories(megahit_server PRIVATE src)
    target_link_libraries(megahit_server PRIVATE Seastar::seastar ${ZLIB_LIBRARIES})
    # NOTE: the reused stage code still uses OpenMP internally; it runs OFF the reactor via seastar::async
    # (assembly_engine.cc). Replacing OpenMP with shard parallelism is a later migration phase.
endif ()
install(DIRECTORY test_data DESTINATION share/${PROJECT_NAME})
