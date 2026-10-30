// Microbenchmarks with wall-clock evidence printed as the machine-readable
// line the checked-in fixture records. One process, steady_clock, medians of
// repeats so a noisy core doesn't flip the contract.
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "latency/probe.hpp"

using namespace latency;
using clock_ = std::chrono::steady_clock;

static double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    constexpr int kReps = 7;
    constexpr int kOps = 1'000'000;
    std::vector<double> insert_ns, quantile_ns, consume_ns;

    for (int r = 0; r < kReps; ++r) {
        Histogram h;
        std::mt19937 rng(1234 + r);
        std::lognormal_distribution<double> lat(6.0, 0.6);  // ~400us median, long tail
        auto t0 = clock_::now();
        for (int i = 0; i < kOps; ++i) h.observe(std::chrono::microseconds((long long)lat(rng)));
        auto t1 = clock_::now();
        insert_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kOps);

        t0 = clock_::now();
        volatile double sink = 0;
        for (int i = 0; i < 1000; ++i) sink += *h.quantile_us(0.99);
        t1 = clock_::now();
        quantile_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / 1000);
        (void)sink;

        TokenBucket b(1e9, 1e6);
        t0 = clock_::now();
        int taken = 0;
        for (int i = 0; i < kOps; ++i) taken += b.consume(1.0) ? 1 : 0;
        t1 = clock_::now();
        consume_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kOps);
        if (taken < kOps / 2) { std::printf("bench: limiter starved (taken=%d)\n", taken); return 2; }
    }

    std::printf("BENCH observe_us_per_op_med=%.3f quantile_us_per_op_med=%.3f consume_us_per_op_med=%.3f\n",
                median_of(insert_ns) / 1000.0, median_of(quantile_ns) / 1000.0, median_of(consume_ns) / 1000.0);
    return 0;
}
