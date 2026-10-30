// Self-checking test binary: exit 0 = all pass, prints one line per case.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "latency/probe.hpp"

using namespace latency;
static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void test_quantile_bounds_the_true_value() {
    Histogram h;
    for (int i = 1; i <= 1000; ++i) h.observe(std::chrono::microseconds(i));
    // p50 of 1..1000 is 500us; the bucket upper bound may overestimate by at
    // most growth-1 (~9.1%), never underestimate.
    auto p50 = h.quantile_us(0.50);
    auto p99 = h.quantile_us(0.99);
    CHECK(p50.has_value());
    CHECK(*p50 >= 500.0);
    CHECK(*p50 <= 500.0 * 1.10);
    CHECK(*p99 >= 990.0);
    CHECK(*p99 <= 990.0 * 1.10);
}

static void test_exact_moments_and_count() {
    Histogram h;
    h.observe(std::chrono::microseconds(10));
    h.observe(std::chrono::microseconds(20));
    h.observe(std::chrono::microseconds(30));
    CHECK(h.count() == 3);
    CHECK(std::abs(h.mean_us() - 20.0) < 1e-9);
    CHECK(h.min_us() == 10.0);
    CHECK(h.max_us() == 30.0);
}

static void test_empty_and_reset() {
    Histogram h;
    CHECK(!h.quantile_us(0.5).has_value());
    h.observe(std::chrono::microseconds(7));
    h.reset();
    CHECK(h.count() == 0);
    CHECK(!h.quantile_us(0.5).has_value());
}

static void test_constant_memory_and_overflow_clamp() {
    Histogram h;  // 512 buckets regardless of input volume or size
    for (int i = 0; i < 100000; ++i) h.observe(std::chrono::microseconds(i));
    h.observe(std::chrono::microseconds(4'000'000'000LL));  // ~67min: clamps to last bucket
    CHECK(h.count() == 100001);
    CHECK(h.quantile_us(1.0).has_value());
}

static void test_gaussian_mass_agrees_with_sorted_truth() {
    // Cross-check the histogram against exact sorted quantiles on a known
    // distribution: same technique as the Rust benchmark fixture, small n.
    std::mt19937 rng(20261029);
    std::normal_distribution<double> dist(1000.0, 100.0);
    std::vector<double> truth;
    Histogram h;
    for (int i = 0;  i < 50000; ++i) {
        double us = dist(rng);
        truth.push_back(us);
        h.observe(std::chrono::microseconds(static_cast<long long>(us)));
    }
    std::sort(truth.begin(), truth.end());
    for (double q : {0.5, 0.9, 0.99, 0.999}) {
        double exact = truth[static_cast<std::size_t>(q * truth.size())];
        double est = *h.quantile_us(q);
        CHECK(est >= exact * 0.95);          // not wildly under (bucket noise aside)
        CHECK(est <= exact * 1.25);          // bounded overestimate, sane headroom
    }
}

static void test_bucket_takes_burst_then_denies() {
    auto t0 = std::chrono::steady_clock::now();
    TokenBucket b(/*rate*/ 10.0, /*capacity*/ 5.0);
    CHECK(b.available(t0) == 5.0);
    for (int i = 0; i < 5; ++i) CHECK(b.consume(1.0, t0));
    CHECK(!b.consume(1.0, t0));  // burst spent
    auto later = t0 + std::chrono::seconds(1);
    CHECK(b.available(later) == 10.0 - 5.0);  // refilled at 10/s for 1s, capped at 5
    CHECK(b.consume(5.0, later));
}

static void test_bucket_never_over_capacity_and_seconds_until() {
    auto t0 = std::chrono::steady_clock::now();
    TokenBucket b(4.0, 4.0);
    auto idle = t0 + std::chrono::seconds(100);
    CHECK(b.available(idle) == 4.0);  // long idle clamps at capacity
    CHECK(b.consume(4.0, idle));
    double wait = b.seconds_until(2.0, idle);
    CHECK(std::abs(wait - 0.5) < 1e-9);  // 2 tokens at 4/s
}

static void test_rejects_nonsense() {
    bool threw = false;
    try { Histogram h(Histogram::Options{/*bucket0*/ 0.0}); } catch (...) { threw = true; }
    CHECK(threw);
    threw = false;
    try { TokenBucket b(0.0, 1.0); } catch (...) { threw = true; }
    CHECK(threw);
}

int main() {
    test_quantile_bounds_the_true_value();
    test_exact_moments_and_count();
    test_empty_and_reset();
    test_constant_memory_and_overflow_clamp();
    test_gaussian_mass_agrees_with_sorted_truth();
    test_bucket_takes_burst_then_denies();
    test_bucket_never_over_capacity_and_seconds_until();
    test_rejects_nonsense();
    if (failures == 0) { std::printf("all probe tests pass\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
