// tests/unit/pulse/metric_test.cpp
//
// Tests for the telemetry primitives (NeuroPulse). Covers the
// Counter atomic-float fix (R1), Gauge CAS-loop, Histogram
// bucket placement, Registry dedup, and Prometheus-style scrape
// output.

#include "neuro/pulse/metric.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::pulse::Counter;
using neuro::pulse::Gauge;
using neuro::pulse::Histogram;
using neuro::pulse::Registry;

// ---- 1. Counter basic operations --------------------------------------

TEST(pulse, counter_inc_default_increments_by_one) {
    Counter c("reqs", "requests served");
    EXPECT_EQ(0.0, c.value());
    c.inc();
    EXPECT_EQ(1.0, c.value());
    c.inc();
    EXPECT_EQ(2.0, c.value());
}

TEST(pulse, counter_inc_with_amount) {
    Counter c("bytes", "bytes sent");
    c.inc(1024.0);
    c.inc(512.0);
    EXPECT_EQ(1536.0, c.value());
}

TEST(pulse, counter_handles_fractions) {
    Counter c("lat_ms", "fractional ms accumulator");
    c.inc(0.5);
    c.inc(0.25);
    c.inc(0.125);
    EXPECT_EQ(0.875, c.value());
}

TEST(pulse, counter_value_does_not_lose_floats_to_bitcast) {
    // Regression test for R1: the old Counter used std::atomic<int64_t>
    // + std::bit_cast<double>, which loses fractional bits.
    Counter c("p", "p");
    c.inc(0.1);
    EXPECT_EQ(0.1, c.value());  // exact equality
}

TEST(pulse, counter_is_thread_safe) {
    // Spawn 8 threads, each inc'ing 1000 times. Final value must
    // be exactly 8000.
    Counter c("shared", "concurrent");
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&c]() {
            for (int i = 0; i < 1000; ++i) c.inc();
        });
    }
    for (auto& th : ts) th.join();
    EXPECT_EQ(8000.0, c.value());
}

// ---- 2. Gauge operations ----------------------------------------------

TEST(pulse, gauge_set_and_read) {
    Gauge g("qdepth", "queue depth");
    EXPECT_EQ(0.0, g.value());
    g.set(7.5);
    EXPECT_EQ(7.5, g.value());
}

TEST(pulse, gauge_inc_dec) {
    Gauge g("qdepth", "queue depth");
    g.set(10.0);
    g.inc();
    EXPECT_EQ(11.0, g.value());
    g.dec(3.0);
    EXPECT_EQ(8.0, g.value());
    g.inc(-2.0);
    EXPECT_EQ(6.0, g.value());
}

TEST(pulse, gauge_inc_can_go_negative) {
    Gauge g("net", "net bytes (signed)");
    g.set(100.0);
    g.dec(150.0);
    EXPECT_EQ(-50.0, g.value());
}

TEST(pulse, gauge_inc_thread_safe) {
    Gauge g("q", "concurrent");
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&g]() {
            for (int i = 0; i < 1000; ++i) g.inc();
        });
    }
    for (auto& th : ts) th.join();
    EXPECT_EQ(8000.0, g.value());
}

// ---- 3. Histogram bucket placement -----------------------------------

TEST(pulse, histogram_buckets) {
    Histogram h("lat", "latency ms", {1.0, 5.0, 10.0, 50.0});
    h.observe(0.5);    // ≤ 1.0
    h.observe(1.0);    // ≤ 1.0
    h.observe(3.0);    // ≤ 5.0
    h.observe(7.0);    // ≤ 10.0
    h.observe(100.0);  // +Inf
    EXPECT_EQ(5u, h.count());
    EXPECT_EQ(0.5 + 1.0 + 3.0 + 7.0 + 100.0, h.sum());
    // buckets: [2, 1, 1, 0, 1]
    EXPECT_EQ(2u, h.buckets()[0]);
    EXPECT_EQ(1u, h.buckets()[1]);
    EXPECT_EQ(1u, h.buckets()[2]);
    EXPECT_EQ(0u, h.buckets()[3]);
    EXPECT_EQ(1u, h.buckets()[4]);  // +Inf
}

TEST(pulse, histogram_empty) {
    Histogram h("lat", "latency ms", {1.0, 10.0});
    EXPECT_EQ(0u, h.count());
    EXPECT_EQ(0.0, h.sum());
    EXPECT_EQ(0u, h.buckets()[0]);
    EXPECT_EQ(0u, h.buckets()[1]);
    EXPECT_EQ(0u, h.buckets()[2]);
}

// ---- 4. Registry dedup ------------------------------------------------

TEST(pulse, registry_dedups_counters) {
    Registry r;
    Counter& a = r.counter("c", "h");
    a.inc(5.0);
    Counter& b = r.counter("c", "h");
    EXPECT_EQ(5.0, b.value());  // same instance
}

TEST(pulse, registry_dedups_gauges) {
    Registry r;
    Gauge& a = r.gauge("g", "h");
    a.set(3.0);
    Gauge& b = r.gauge("g", "h");
    EXPECT_EQ(3.0, b.value());
}

TEST(pulse, registry_dedups_histograms) {
    Registry r;
    Histogram& a = r.histogram("h", "help", {1.0});
    a.observe(0.5);
    Histogram& b = r.histogram("h", "help", {1.0});
    EXPECT_EQ(1u, b.count());
}

// ---- 5. scrape() output ------------------------------------------------

TEST(pulse, scrape_includes_counter_help_and_value) {
    Registry r;
    Counter& c = r.counter("myreqs", "total requests");
    c.inc(7.0);
    auto out = r.scrape();
    EXPECT_TRUE(out.find("# HELP myreqs total requests") != std::string::npos);
    EXPECT_TRUE(out.find("# TYPE myreqs counter") != std::string::npos);
    EXPECT_TRUE(out.find("myreqs 7") != std::string::npos);
}

TEST(pulse, scrape_includes_gauge) {
    Registry r;
    Gauge& g = r.gauge("depth", "queue depth");
    g.set(2.5);
    auto out = r.scrape();
    EXPECT_TRUE(out.find("# TYPE depth gauge") != std::string::npos);
    EXPECT_TRUE(out.find("depth 2.5") != std::string::npos);
}

TEST(pulse, scrape_includes_histogram_buckets) {
    Registry r;
    Histogram& h = r.histogram("lat", "latency ms", {1.0, 10.0});
    h.observe(0.5);
    h.observe(5.0);
    h.observe(50.0);
    auto out = r.scrape();
    EXPECT_TRUE(out.find("# TYPE lat histogram") != std::string::npos);
    EXPECT_TRUE(out.find("lat_bucket{le=\"1\"}") != std::string::npos);
    EXPECT_TRUE(out.find("lat_bucket{le=\"10\"}") != std::string::npos);
    EXPECT_TRUE(out.find("lat_bucket{le=\"+Inf\"}") != std::string::npos);
    EXPECT_TRUE(out.find("lat_count 3") != std::string::npos);
    EXPECT_TRUE(out.find("lat_sum 55.5") != std::string::npos);
}

TEST(pulse, scrape_orders_metrics_alphabetically) {
    Registry r;
    r.counter("zeta", "z");
    r.counter("alpha", "a");
    r.counter("mu", "m");
    auto out = r.scrape();
    auto a = out.find("alpha");
    auto m = out.find("mu");
    auto z = out.find("zeta");
    EXPECT_TRUE(a != std::string::npos);
    EXPECT_TRUE(m != std::string::npos);
    EXPECT_TRUE(z != std::string::npos);
    EXPECT_TRUE(a < m);
    EXPECT_TRUE(m < z);
}

TEST(pulse, host_registry_is_singleton) {
    auto& a = neuro::pulse::host_registry();
    auto& b = neuro::pulse::host_registry();
    EXPECT_EQ(&a, &b);
}

RUN_ALL_TESTS()