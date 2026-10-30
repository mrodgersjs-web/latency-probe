# latency-probe

Header-only C++17 latency histogram (exponential log buckets, constant memory)
and token-bucket rate limiter. Everything is in `include/latency/probe.hpp`;
tests are a self-checking binary in `tests/probe_tests.cpp`, benchmarks in
`bench/bench.cpp`, checked-in performance evidence in
`fixtures/bench-baseline.json`.

## Setup and test (exact commands)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/probe_tests    # this is the repo smoke; exit 0 = pass
./build/probe_bench
```

## Rules

- stdlib only. No third-party dependencies, no exceptions disabled.
- Quantiles are documented upper bounds; do not "fix" them to interpolate
  without changing the tests and the README claim together.
- The benchmark fixture is part of the contract: performance regressions are
  reviewable because the numbers live in the repo.
