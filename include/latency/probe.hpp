// latency-probe: p99/p999 latency histograms and a token-bucket limiter.
// Header-only C++17, no dependencies beyond the standard library.
//
// Design notes (the parts an interview asks about):
//  - The histogram uses exponential log-bucketing: constant memory, bounded
//    relative error, insert is O(1). Full sorted storage is O(n) memory and
//    exact; for a serving layer watching a million requests a minute, exact
//    loses to bounded. Quantiles are reported as bucket upper bounds, which
//    overestimate by at most the bucket's relative width (default ~12.2%).
//  - The limiter is a classic token bucket: refill is computed lazily from a
//    monotonic clock delta, so there is no timer thread and no drift between
//    refill ticks and consumption. Burst is capped by capacity by construction.
//  - Both are thread-compatible, not thread-safe: callers own the mutex. A
//    lock-free variant trades this simplicity for false sharing on the
//    counters; at our rate (thousands/s, not millions) the mutex is free.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace latency {

// Exponential-bucket latency histogram, microseconds on input.
//
// Buckets: [0us] is bucket 0; then bucket k covers
// [base^(k-1) * bucket0_width, base^k * bucket0_width). With the defaults
// (bucket0 = 1us, base = 2^(1/8) ~= 1.0905) the relative error on a quantile
// is < 9.1% and 512 buckets cover up to ~1e24 ns, so overflow is unreachable.
class Histogram {
  public:
    struct Options {
        double bucket0_width_us = 1.0;  // width of bucket 1
        double growth = 1.0905077326652577;  // 2^(1/8)
        std::size_t num_buckets = 512;
    };

    Histogram() : Histogram(Options{}) {}
    explicit Histogram(const Options& opts)
        : opts_(opts), counts_(opts.num_buckets, 0), total_(0),
          sum_us_(0.0), min_us_(0.0), max_us_(0.0) {
        if (opts_.bucket0_width_us <= 0.0) {
            throw std::invalid_argument("bucket0_width_us must be > 0");
        }
        if (opts_.growth <= 1.0) {
            throw std::invalid_argument("growth must be > 1");
        }
        if (opts_.num_buckets < 2) {
            throw std::invalid_argument("num_buckets must be >= 2");
        }
    }

    void observe(std::chrono::microseconds value) {
        double us = static_cast<double>(value.count());
        if (us < 0.0) {
            throw std::invalid_argument("negative latency");
        }
        counts_[bucket_of(us)] += 1;
        total_ += 1;
        sum_us_ += us;
        if (total_ == 1 || us < min_us_) min_us_ = us;
        if (total_ == 1 || us > max_us_) max_us_ = us;
    }

    // Observe the time elapsed since a steady time_point.
    template <typename Duration>
    void observe_since(std::chrono::time_point<std::chrono::steady_clock, Duration> since) {
        observe(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - since));
    }

    // Quantile in [0,1]. Returns the upper edge of the bucket holding the
    // q-quantile: an upper bound on the true value (overestimate <= growth-1).
    std::optional<double> quantile_us(double q) const {
        if (total_ == 0) return std::nullopt;
        if (q < 0.0 || q > 1.0) throw std::invalid_argument("q must be in [0,1]");
        std::uint64_t target = static_cast<std::uint64_t>(q * static_cast<double>(total_));
        if (target == 0) target = 1;  // q=0 or tiny q: report the smallest bucket seen
        std::uint64_t running = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            running += counts_[i];
            if (running >= target) return edge_us(i);
        }
        return edge_us(counts_.size() - 1);
    }

    double mean_us() const {
        return total_ == 0 ? 0.0 : sum_us_ / static_cast<double>(total_);
    }
    double min_us() const { return min_us_; }
    double max_us() const { return max_us_; }
    std::uint64_t count() const { return total_; }

    void reset() {
        std::fill(counts_.begin(), counts_.end(), std::uint64_t{0});
        total_ = 0;
        sum_us_ = 0.0;
    }

  private:
    std::size_t bucket_of(double us) const {
        if (us <= opts_.bucket0_width_us) return 0;
        // log(us / bucket0) / log(growth) + 1, clamped to the table
        double k = std::log(us / opts_.bucket0_width_us) / std::log(opts_.growth) + 1.0;
        if (k < 1.0) k = 1.0;
        auto idx = static_cast<std::size_t>(k);
        return idx >= counts_.size() ? counts_.size() - 1 : idx;
    }
    double edge_us(std::size_t bucket) const {
        if (bucket == 0) return opts_.bucket0_width_us;
        return opts_.bucket0_width_us * std::pow(opts_.growth, static_cast<double>(bucket));
    }

    Options opts_;
    std::vector<std::uint64_t> counts_;
    std::uint64_t total_;
    double sum_us_;
    double min_us_;
    double max_us_;
};

// RAIA-style stopwatch: scope ends, sample lands in the histogram.
class Scope {
  public:
    Scope(Histogram& h, std::chrono::steady_clock::time_point start =
                            std::chrono::steady_clock::now())
        : h_(h), start_(start) {}
    ~Scope() {
        h_.observe(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_));
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    Histogram& h_;
    std::chrono::steady_clock::time_point start_;
};

// Token bucket: rate tokens/second, burst capacity tokens.
// Consume is O(1); refill happens at the top of consume from the clock delta,
// so idle periods refill fully and no timer thread exists.
class TokenBucket {
  public:
    TokenBucket(double rate_per_sec, double capacity)
        : rate_(rate_per_sec), capacity_(capacity), tokens_(capacity),
          last_(std::chrono::steady_clock::now()) {
        if (rate_per_sec <= 0.0) throw std::invalid_argument("rate must be > 0");
        if (capacity <= 0.0) throw std::invalid_argument("capacity must be > 0");
    }

    // Take n tokens if available; returns false (and takes nothing) otherwise.
    bool consume(double n = 1.0,
                 std::chrono::steady_clock::time_point now =
                     std::chrono::steady_clock::now()) {
        refill(now);
        if (tokens_ >= n) {
            tokens_ -= n;
            return true;
        }
        return false;
    }

    double available(std::chrono::steady_clock::time_point now =
                         std::chrono::steady_clock::now()) {
        refill(now);
        return tokens_;
    }

    // Whole seconds until n tokens exist; 0 if it can be served now.
    double seconds_until(double n,
                         std::chrono::steady_clock::time_point now =
                             std::chrono::steady_clock::now()) {
        refill(now);
        double missing = n - tokens_;
        return missing <= 0.0 ? 0.0 : missing / rate_;
    }

  private:
    void refill(std::chrono::steady_clock::time_point now) {
        double dt = std::chrono::duration<double>(now - last_).count();
        if (dt > 0.0) {
            tokens_ += dt * rate_;
            if (tokens_ > capacity_) tokens_ = capacity_;
            last_ = now;
        }
    }

    double rate_;
    double capacity_;
    double tokens_;
    std::chrono::steady_clock::time_point last_;
};

}  // namespace latency
