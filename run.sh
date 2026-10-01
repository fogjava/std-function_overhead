#!/usr/bin/env bash
# Configure, build and run dispatch_bench. Extra args go to the benchmark,
# e.g. ./run.sh --benchmark_filter=Deferred
# No core pinning on purpose: live trading has its own CPU policy.
set -euo pipefail
cd "$(dirname "$0")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j

export TZ=${TZ:-:/etc/localtime}
./build/dispatch_bench \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  "$@"
