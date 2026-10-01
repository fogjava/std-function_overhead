// Benchmark: DispatchPtr deleter strategies
//
//   StdFunction : std::unique_ptr<const char, std::function<void(const char*)>>   (current)
//   Inplace     : std::unique_ptr<const char, stdext::inplace_function<..., 24, 8>>
//   Concrete    : std::unique_ptr<const char, DispatchDeleter>                      (proposed)
//
// Two scenarios:
//   Immediate : handler drops the DispatchPtr right away (release inside the call)
//   Deferred  : handler moves the DispatchPtr into a queue, released later in batches
//
// Build (fetches Google Benchmark and SG14 inplace_function.h):
//   cmake -S . -B build && cmake --build build -j   (or ./run.sh)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <vector>

#include "inplace_function.h"

// ---------------------------------------------------------------------------
// Allocation counter (global operator new override)
// ---------------------------------------------------------------------------
static std::size_t g_allocs = 0;

void* operator new(std::size_t n) {
  ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ---------------------------------------------------------------------------
// Minimal stand-ins for your types
// ---------------------------------------------------------------------------
enum class SignalIdType : std::uint8_t { DATA, TOK_RELEASE };

using TokenT  = std::uint64_t;
using OffsetT = std::uint64_t;

struct Signal {
  std::uint32_t topic_id;
  SignalIdType  id;
  TokenT        token;
  OffsetT       data_offset;
};

// Proposed concrete deleter
struct DispatchDeleter {
  using ReleaseFn = void (*)(void* ctx, TokenT token, OffsetT data_offset) noexcept;

  ReleaseFn release = nullptr;
  void*     ctx = nullptr;
  TokenT    token{};
  OffsetT   data_offset{};

  void operator()(const char*) const noexcept { release(ctx, token, data_offset); }
};

enum class Mode { StdFunction, Inplace, Concrete };

template <Mode M> struct DeleterFor;
template <> struct DeleterFor<Mode::StdFunction> { using type = std::function<void(const char*)>; };
template <> struct DeleterFor<Mode::Inplace>     { using type = stdext::inplace_function<void(const char*), 24, alignof(void*)>; };
template <> struct DeleterFor<Mode::Concrete>    { using type = DispatchDeleter; };

// ---------------------------------------------------------------------------
// Reactor model: builds a DispatchPtr per signal and hands it to the handler
// ---------------------------------------------------------------------------
template <Mode M>
class Reactor {
 public:
  using DispatchPtr   = std::unique_ptr<const char, typename DeleterFor<M>::type>;
  using SignalHandler = std::function<void(DispatchPtr data)>;

  explicit Reactor(SignalHandler h) : handler_(std::move(h)) {}

  void Dispatch(const Signal& signal) { handler_(MakePtr(signal)); }

  std::uint64_t released() const { return released_count_; }

 private:
  DispatchPtr MakePtr(const Signal& signal) {
    const char* p = membase_ + signal.data_offset;
    if constexpr (M == Mode::Concrete) {
      return DispatchPtr(
          p, DispatchDeleter{&Reactor::ReleaseToken, this, signal.token, signal.data_offset});
    } else {
      // Same lambda as your production call site
      return DispatchPtr(
          p, [this, token = signal.token, data_offset = signal.data_offset](const char*) {
            this->PublishSignal(
                Signal{pub_topic_id_, SignalIdType::TOK_RELEASE, token, data_offset});
          });
    }
  }

  static void ReleaseToken(void* ctx, TokenT token, OffsetT off) noexcept {
    auto* self = static_cast<Reactor*>(ctx);
    self->PublishSignal(Signal{self->pub_topic_id_, SignalIdType::TOK_RELEASE, token, off});
  }

  // noinline so the release work isn't folded away; stands in for the real publish
  __attribute__((noinline)) void PublishSignal(Signal s) noexcept {
    checksum_ += s.token ^ s.data_offset ^ s.topic_id;
    ++released_count_;
    benchmark::DoNotOptimize(checksum_);
  }

  SignalHandler handler_;
  std::uint32_t pub_topic_id_ = 7;
  std::uint64_t checksum_ = 0;
  std::uint64_t released_count_ = 0;
  alignas(64) char membase_[4096]{};
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
constexpr std::size_t kSignalCount = 1024;  // power of two
constexpr std::size_t kBatch = 64;

static std::vector<Signal> MakeSignals() {
  std::vector<Signal> v;
  v.reserve(kSignalCount);
  for (std::size_t i = 0; i < kSignalCount; ++i) {
    v.push_back(Signal{1, SignalIdType::DATA, 0x1000 + i, (i * 64) % 4096});
  }
  return v;
}

template <Mode M>
static void Finish(benchmark::State& state, const Reactor<M>& r) {
  state.counters["allocs/op"] =
      benchmark::Counter(static_cast<double>(g_allocs), benchmark::Counter::kAvgIterations);
  state.counters["sizeof(ptr)"] = sizeof(typename Reactor<M>::DispatchPtr);
  benchmark::DoNotOptimize(r.released());
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------
template <Mode M>
static void BM_Immediate(benchmark::State& state) {
  using Ptr = typename Reactor<M>::DispatchPtr;
  const auto signals = MakeSignals();
  Reactor<M> reactor([](Ptr data) { benchmark::DoNotOptimize(data.get()); });

  std::size_t i = 0;
  g_allocs = 0;
  for (auto _ : state) {
    reactor.Dispatch(signals[i++ & (kSignalCount - 1)]);
  }
  Finish(state, reactor);
}

template <Mode M>
static void BM_Deferred(benchmark::State& state) {
  using Ptr = typename Reactor<M>::DispatchPtr;
  const auto signals = MakeSignals();
  std::vector<Ptr> pending;
  pending.reserve(kBatch);
  Reactor<M> reactor([&pending](Ptr data) { pending.push_back(std::move(data)); });

  std::size_t i = 0;
  g_allocs = 0;
  for (auto _ : state) {
    reactor.Dispatch(signals[i++ & (kSignalCount - 1)]);
    if (pending.size() == kBatch) pending.clear();  // releases the batch
  }
  pending.clear();
  Finish(state, reactor);
}

BENCHMARK_TEMPLATE(BM_Immediate, Mode::StdFunction);
BENCHMARK_TEMPLATE(BM_Immediate, Mode::Inplace);
BENCHMARK_TEMPLATE(BM_Immediate, Mode::Concrete);
BENCHMARK_TEMPLATE(BM_Deferred, Mode::StdFunction);
BENCHMARK_TEMPLATE(BM_Deferred, Mode::Inplace);
BENCHMARK_TEMPLATE(BM_Deferred, Mode::Concrete);

int main(int argc, char** argv) {
  std::printf("sizeof(DispatchPtr): std::function=%zu  inplace_function=%zu  concrete=%zu\n\n",
              sizeof(Reactor<Mode::StdFunction>::DispatchPtr),
              sizeof(Reactor<Mode::Inplace>::DispatchPtr),
              sizeof(Reactor<Mode::Concrete>::DispatchPtr));
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
