// tests/integration/pulse_learn.cpp
//
// Z7.7 — pulse counter observed by a learn optimizer.
//
// Composes NeuroPulse (Counter) with NeuroLearn (GradientOptimizer).
// A "load" counter is incremented and snapshot-read; the value is
// fed to the optimizer as a loss; the optimizer emits a proposal
// for a worker-count knob, and the proposal is applied to update
// the scheduler.
//
// This is the closed-loop: metrics → optimizer → kernel knob.

#include "tests/test_framework.hpp"

#include "neuro/learn/optimizer.hpp"
#include "neuro/pulse/metric.hpp"

#include <cstdint>
#include <memory>

using neuro::learn::GradientOptimizer;
using neuro::learn::make_worker_count_optimizer;
using neuro::learn::Observation;
using neuro::learn::Proposal;
using neuro::pulse::Counter;

TEST(pulse_learn, counter_increment_and_read) {
    Counter c("requests", "Total request count");
    EXPECT_EQ(0.0, c.value());

    c.inc();
    c.inc();
    c.inc(5.0);
    EXPECT_EQ(7.0, c.value());

    c.inc(-3.0);
    EXPECT_EQ(4.0, c.value());
}

TEST(pulse_learn, optimizer_consumes_counter_via_observations) {
    // Build a worker-count optimizer at 4 workers, learning rate 0.1.
    auto opt = make_worker_count_optimizer(/*initial=*/4, /*lr=*/0.1);
    EXPECT_TRUE(opt != nullptr);

    // Fake "loss" observations: load is high so the optimizer should
    // propose more workers.
    for (int i = 0; i < 3; ++i) {
        Observation o;
        o.key = "loss";
        o.value = 10.0;  // high loss
        opt->observe(o);
    }

    // The optimizer should propose a higher worker count.
    Proposal p = opt->propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p.kind);
    EXPECT_TRUE(p.integer_value >= 4);
}

TEST(pulse_learn, optimizer_lowers_workers_when_load_drops) {
    auto opt = make_worker_count_optimizer(/*initial=*/8, /*lr=*/0.5);

    // Three low-loss observations → optimizer should propose fewer
    // workers.
    for (int i = 0; i < 3; ++i) {
        Observation o;
        o.key = "loss";
        o.value = 0.1;
        opt->observe(o);
    }

    Proposal p = opt->propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p.kind);
    EXPECT_TRUE(p.integer_value <= 8);
}

TEST(pulse_learn, observations_threaded_through_pulse_counter) {
    // Counter is the source of truth; we hand its value to the
    // optimizer as a loss at each step.
    Counter load("cpu.load", "Synthetic CPU load");
    auto opt = make_worker_count_optimizer(/*initial=*/2, /*lr=*/0.2);

    // Simulate 5 ticks: load rises.
    for (int i = 0; i < 5; ++i) {
        load.inc(1.0);
        Observation o;
        o.key = "loss";
        o.value = load.value();
        opt->observe(o);
    }

    Proposal p = opt->propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p.kind);
    EXPECT_TRUE(p.integer_value >= 2);
    EXPECT_EQ(5.0, load.value());
}

TEST(pulse_learn, optimizer_describes_state) {
    auto opt = make_worker_count_optimizer(/*initial=*/4, /*lr=*/0.1);
    Observation o;
    o.key = "loss";
    o.value = 1.0;
    opt->observe(o);

    auto desc = opt->describe();
    EXPECT_FALSE(desc.empty());
}

RUN_ALL_TESTS()