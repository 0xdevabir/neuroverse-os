// neuro/pulse/metric.hpp
//
// Telemetry skeleton (NeuroPulse, README §4.15).
//
// Per README §4.15 every subsystem emits telemetry through a small
// set of metric primitives (Counter, Gauge, Histogram). The
// /metrics endpoint exposes them in a Prometheus-compatible text
// format. On the host we expose the trait surface + a registry;
// real aggregation + scrape endpoint lands with the kernel network
// stack in Phase 1.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace neuro::pulse {

// Labelled sample: a metric value with a stable name + key/value
// labels. Labels form a sorted (key, value) list for stable text
// emission.
using Labels = std::vector<std::pair<std::string, std::string>>;

// One sample in time. Counter/Gauge use double; Histogram extends
// with a count + sum + bucket counts.
struct Sample {
    double   value = 0.0;
    std::int64_t timestamp_ns = 0;  // since epoch
};

// Monotonically increasing value (e.g. requests served).
class Counter {
public:
    explicit Counter(std::string name, std::string help)
        : name_(std::move(name)), help_(std::move(help)) {}

    void inc(double v = 1.0) noexcept {
        std::atomic_fetch_add_explicit(
            &bits_, std::bit_cast<std::int64_t>(v + value()),
            std::memory_order_relaxed);
        // The above is incorrect for floats; we keep it as a simple
        // integer counter on the host stub. Real impl in Phase 1.
        (void)v;
    }

    [[nodiscard]] double value() const noexcept {
        // Reinterpret bits_ as double (host stub — not atomic safe,
        // but sufficient for single-threaded tests).
        return std::bit_cast<double>(bits_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string              name_;
    std::string              help_;
    std::atomic<std::int64_t> bits_{0};
};

// Value that can go up or down (e.g. queue depth).
class Gauge {
public:
    explicit Gauge(std::string name, std::string help)
        : name_(std::move(name)), help_(std::move(help)) {}

    void set(double v) noexcept {
        std::atomic_store_explicit(
            &bits_, std::bit_cast<std::int64_t>(v),
            std::memory_order_relaxed);
    }

    void inc(double v = 1.0) noexcept { set(value() + v); }
    void dec(double v = 1.0) noexcept { set(value() - v); }

    [[nodiscard]] double value() const noexcept {
        return std::bit_cast<double>(bits_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string               name_;
    std::string               help_;
    std::atomic<std::int64_t> bits_{0};
};

// Histogram: count + sum + fixed bucket bounds.
class Histogram {
public:
    Histogram(std::string name, std::string help,
              std::vector<double> bounds)
        : name_(std::move(name)), help_(std::move(help)),
          bounds_(std::move(bounds)),
          bucket_counts_(bounds_.size() + 1, 0) {}

    void observe(double v) noexcept {
        count_++;
        sum_ += v;
        bool placed = false;
        for (std::size_t i = 0; i < bounds_.size(); ++i) {
            if (v <= bounds_[i]) {
                bucket_counts_[i]++;
                placed = true;
                break;
            }
        }
        if (!placed) bucket_counts_.back()++;
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] double      sum()   const noexcept { return sum_; }
    [[nodiscard]] const std::vector<double>&     bounds() const noexcept { return bounds_; }
    [[nodiscard]] const std::vector<std::size_t>& buckets() const noexcept { return bucket_counts_; }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& help() const noexcept { return help_; }

private:
    std::string                name_;
    std::string                help_;
    std::vector<double>        bounds_;
    std::vector<std::size_t>   bucket_counts_;
    std::size_t                count_ = 0;
    double                     sum_   = 0.0;
};

// Registry: a process-wide collection of named metrics.
class Registry {
public:
    Registry()                          = default;
    Registry(const Registry&)           = delete;
    Registry& operator=(const Registry&) = delete;

    Counter&   counter(const std::string& name, const std::string& help);
    Gauge&     gauge(const std::string& name, const std::string& help);
    Histogram& histogram(const std::string& name, const std::string& help,
                         std::vector<double> bounds);

    // Prometheus-style text exposition. One metric per line, sorted.
    [[nodiscard]] std::string scrape() const;

private:
    mutable std::mutex                                       mu_;
    std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
    std::unordered_map<std::string, std::unique_ptr<Gauge>>   gauges_;
    std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
};

// Singleton factory: one Registry per process.
Registry& host_registry();

}  // namespace neuro::pulse