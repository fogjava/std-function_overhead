cmake_minimum_required(VERSION 3.16)
project(dispatch_bench LANGUAGES CXX)

# DispatchPtr deleter strategies: std::function vs stdext::inplace_function vs a
# concrete DispatchDeleter struct. All dependencies are fetched here; nothing
# comes from the navi tree.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Where to download from. Override with the corp proxy if GitHub is unreachable:
#   -DDISPATCH_BENCH_GITHUB=https://nexus-repo.corp.prestolabs.io/repository/github-proxy
set(DISPATCH_BENCH_GITHUB "https://github.com" CACHE STRING "GitHub base URL (or a proxy of it)")
set(DISPATCH_BENCH_GITHUB_RAW "https://raw.githubusercontent.com" CACHE STRING
    "raw.githubusercontent.com base URL (or a proxy of it)")

include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)  # extracted files get the extraction time
endif()

# ---- Google Benchmark 1.8.3 ------------------------------------------------------
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
  URL ${DISPATCH_BENCH_GITHUB}/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
  URL_HASH SHA256=6bc180a57d23d4d9515519f92b0c83d61b05b5bab188961f36ac7b06b0d9e9ce)
FetchContent_MakeAvailable(benchmark)

# ---- SG14 stdext::inplace_function (single header, pinned commit) ----------------
set(SG14_COMMIT c92614381100bd23f3f0de7882aa455797255c03)
set(SG14_INCLUDE_DIR ${CMAKE_BINARY_DIR}/_deps/sg14/include)
file(DOWNLOAD
  ${DISPATCH_BENCH_GITHUB_RAW}/WG21-SG14/SG14/${SG14_COMMIT}/SG14/inplace_function.h
  ${SG14_INCLUDE_DIR}/inplace_function.h
  EXPECTED_HASH SHA256=65572a722185436a4af8563ad53cac2e494a80f5ffed72da1c149c7aaebf5da0
  TLS_VERIFY ON)

find_package(Threads REQUIRED)

add_executable(dispatch_bench src/dispatch_bench.cpp)
target_compile_options(dispatch_bench PRIVATE -O2 -Wall)
target_include_directories(dispatch_bench PRIVATE ${SG14_INCLUDE_DIR})
target_link_libraries(dispatch_bench PRIVATE benchmark::benchmark Threads::Threads)
