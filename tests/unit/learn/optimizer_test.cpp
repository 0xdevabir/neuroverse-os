// tests/unit/learn/optimizer_test.cpp
//
// Tests for the gradient-descent optimizer (Q1). The optimizer is
// a plain SGD-with-momentum-buffer; we verify:
//   - Initial state is preserved until a step is taken.
//   - Negative gradient → param increases (with lr > 0).
//   - Positive gradient → param decreases.
//   - Multiple grad observations are averaged into one step.
//   - Loss observations are recorded.
//   - describe() is human-readable and includes loss.
//   - make_worker_count_optimizer() returns a usable Optimizer.

#include "neuro/learn/optimizer.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::learn::GradientOptimizer;
using neuro::learn::Observation;
using neuro::learn::ObservationValue;
using neuro::learn::Proposal;
using neuro::learn::make_worker_count_optimizer;

namespace {

// Helper: build a single-parameter GradientOptimizer for testing.
GradientOptimizer make_test_opt(double initial, double lr) {
    std::vector<double> p{initial};
    std::vector<GradientOptimizer::ParamSpec> s{
        {"worker_count", Proposal::Kind::SetWorkerCount},
    };
    return GradientOptimizer{lr, std::move(p), std::move(s)};
}

Observation grad_obs(std::int64_t g) {
    Observation o;
    o.key = "grad";
    o.value = std::vector<std::int64_t>{g};
    return o;
}

Observation loss_obs(double l) {
    Observation o;
    o.key   = "loss";
    o.value = l;
    return o;
}

}  // namespace

// ---- 1. Initial state --------------------------------------------------

TEST(grad_opt, initial_state_emits_initial_value) {
    auto opt = make_test_opt(/*initial=*/4.0, /*lr=*/0.1);
    EXPECT_EQ(1u, opt.size());
    EXPECT_EQ(4.0, opt.param(0));
    Proposal p = opt.propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p.kind);
    EXPECT_EQ(4, p.integer_value);
}

// ---- 2. Negative gradient increases parameter --------------------------

TEST(grad_opt, negative_gradient_increases_param) {
    auto opt = make_test_opt(/*initial=*/10.0, /*lr=*/0.1);
    opt.observe(grad_obs(-1));   // grad = -1 → step = +lr*1 = +0.1
    Proposal p = opt.propose();
    EXPECT_EQ(10.1, opt.param(0));
    EXPECT_EQ(10, p.integer_value);  // truncates
}

// ---- 3. Positive gradient decreases parameter --------------------------

TEST(grad_opt, positive_gradient_decreases_param) {
    auto opt = make_test_opt(/*initial=*/10.0, /*lr=*/0.1);
    opt.observe(grad_obs(+5));    // step = -0.1*5 = -0.5
    (void)opt.propose();
    EXPECT_EQ(9.5, opt.param(0));
}

// ---- 4. Multiple grad observations are averaged -------------------------

TEST(grad_opt, multiple_grads_are_averaged) {
    auto opt = make_test_opt(/*initial=*/0.0, /*lr=*/1.0);
    // grads: +2, +4, +6 → avg = +4, step = -1*4 = -4
    opt.observe(grad_obs(+2));
    opt.observe(grad_obs(+4));
    opt.observe(grad_obs(+6));
    (void)opt.propose();
    EXPECT_EQ(-4.0, opt.param(0));
}

// ---- 5. After propose() the grad buffer resets --------------------------

TEST(grad_opt, grad_buffer_resets_after_propose) {
    auto opt = make_test_opt(/*initial=*/10.0, /*lr=*/0.1);
    opt.observe(grad_obs(+10));
    (void)opt.propose();          // consumes the grad
    EXPECT_EQ(9.0, opt.param(0));
    // Now propose() again with no fresh grad → emit current value.
    Proposal p = opt.propose();
    EXPECT_EQ(9, p.integer_value);
}

// ---- 6. Loss observation is recorded -----------------------------------

TEST(grad_opt, loss_observation_recorded) {
    auto opt = make_test_opt(1.0, 0.1);
    opt.observe(loss_obs(2.5));
    auto desc = opt.describe();
    EXPECT_TRUE(desc.find("loss=2.5") != std::string::npos);
}

// ---- 7. Integer loss observations are coerced to double -----------------

TEST(grad_opt, integer_loss_coerced) {
    auto opt = make_test_opt(1.0, 0.1);
    Observation o;
    o.key   = "loss";
    o.value = std::int64_t{7};
    opt.observe(o);
    auto desc = opt.describe();
    EXPECT_TRUE(desc.find("loss=7") != std::string::npos);
}

// ---- 8. Multi-parameter optimizer --------------------------------------

TEST(grad_opt, multi_param_step) {
    std::vector<double> p{0.0, 0.0};
    std::vector<GradientOptimizer::ParamSpec> s{
        {"worker_count", Proposal::Kind::SetWorkerCount},
        {"quantum_us",   Proposal::Kind::SetSchedulingQuantum},
    };
    GradientOptimizer opt{0.5, std::move(p), std::move(s)};
    EXPECT_EQ(2u, opt.size());
    opt.observe([]() {
        Observation o;
        o.key = "grad";
        o.value = std::vector<std::int64_t>{2, 4};
        return o;
    }());
    (void)opt.propose();
    EXPECT_EQ(-1.0, opt.param(0));
    EXPECT_EQ(-2.0, opt.param(1));
}

// ---- 9. describe() includes all parameters ------------------------------

TEST(grad_opt, describe_includes_params) {
    auto opt = make_test_opt(3.5, 0.01);
    auto desc = opt.describe();
    EXPECT_TRUE(desc.find("gradient_optimizer") != std::string::npos);
    EXPECT_TRUE(desc.find("worker_count") != std::string::npos);
    EXPECT_TRUE(desc.find("3.5") != std::string::npos);
}

// ---- 10. Factory returns a working optimizer ---------------------------

TEST(grad_opt, worker_count_factory_emits_set_worker_count) {
    auto opt = make_worker_count_optimizer(/*initial=*/8, /*lr=*/0.5);
    EXPECT_EQ(1u, opt->size());
    opt->observe(grad_obs(-2));   // step = +1
    Proposal p = opt->propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p.kind);
    EXPECT_EQ(9, p.integer_value);
}

// ---- 11. Empty optimizer emits no-op proposal ---------------------------

TEST(grad_opt, empty_propose_emits_default_proposal) {
    std::vector<double> p;
    std::vector<GradientOptimizer::ParamSpec> s;
    GradientOptimizer opt{0.1, std::move(p), std::move(s)};
    EXPECT_EQ(0u, opt.size());
    Proposal p_out = opt.propose();
    EXPECT_EQ(Proposal::Kind::SetWorkerCount, p_out.kind);  // default
    EXPECT_EQ(0, p_out.integer_value);
}

RUN_ALL_TESTS()