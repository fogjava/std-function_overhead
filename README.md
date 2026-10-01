# std-function_overhead
# dispatch_bench

Compares three deleter types for the reactor's `DispatchPtr`
(`std::unique_ptr<const char, Deleter>`):

| Mode | Deleter |
|---|---|
| `StdFunction` | `std::function<void(const char*)>` (the original navi code) |
| `Inplace` | `stdext::inplace_function<void(const char*), 24, 8>` (SG14) |
| `Concrete` | `DispatchDeleter{release fn ptr, ctx, token, data_offset}` (now in navi `reactor.h`) |

Each one runs in two scenarios:
- `Immediate`: the handler drops the pointer inside the call.
- `Deferred`: the handler moves the pointer into a vector, and the vector is cleared every 64 messages.

The `allocs/op` counter counts `operator new` calls per message.

## Build and run

```
./run.sh                                   # configure + build + 5 repetitions
./run.sh --benchmark_filter=Deferred       # extra args go to the benchmark
```

Or build it by hand:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dispatch_bench
```

Dependencies are downloaded at configure time:
- Google Benchmark 1.8.3
- SG14 `inplace_function.h`, pinned to commit `c9261438`

Both downloads are checked against a SHA-256 hash. If GitHub can't be reached, pass
`-DDISPATCH_BENCH_GITHUB=<proxy>` and `-DDISPATCH_BENCH_GITHUB_RAW=<proxy>`.

The flags are `-O2`, GCC 8.5 and glibc malloc. Threads aren't pinned to cores, on purpose.

## First results (2026-09-29, median of 5, ns per message)

| | StdFunction | Inplace | Concrete |
|---|---|---|---|
| Immediate | 36.1 (1 alloc) | 15.3 | 3.3 |
| Deferred | 29.2 (1 alloc) | 15.7 | 12.3 |

`sizeof(DispatchPtr)` is 40 bytes for all three.
