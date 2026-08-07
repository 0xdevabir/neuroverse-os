// tests/unit/proof/contract_test.cpp
//
// Tests for the NeuroProof contract attributes. Covers:
//   - Contract::to_string() formats each field
//   - empty Contract formats to empty string
//   - with_precondition() invokes the wrapped function on success
//   - with_strict_precondition() throws ContractError on failure
//   - with_strict_precondition() returns the wrapped result on success
//   - ContractStats counters increment on failure
//   - reset_contract_stats() clears the counters
//   - NEURO_EXPECTS / NEURO_ENSURES macros expand (compilation test)

#include "neuro/proof/contract.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include "../../test_framework.hpp"

using neuro::proof::Contract;
using neuro::proof::ContractError;
using neuro::proof::ContractStats;
using neuro::proof::reset_contract_stats;
using neuro::proof::with_precondition;
using neuro::proof::with_strict_precondition;

// ---- 1. Contract::to_string() formatting ------------------------------

TEST(contract, to_string_includes_expects_and_ensures) {
    Contract c;
    c.expects = "x > 0";
    c.ensures = "result >= 0";
    auto s = c.to_string();
    EXPECT_TRUE(s.find("expects: x > 0") != std::string::npos);
    EXPECT_TRUE(s.find("ensures: result >= 0") != std::string::npos);
}

TEST(contract, to_string_includes_invariant_and_measure) {
    Contract c;
    c.invariant = "i < n";
    c.measure   = "n - i";
    auto s = c.to_string();
    EXPECT_TRUE(s.find("invariant: i < n") != std::string::npos);
    EXPECT_TRUE(s.find("measure: n - i") != std::string::npos);
}

TEST(contract, empty_contract_is_empty_string) {
    Contract c;
    EXPECT_EQ("", c.to_string());
}

// ---- 2. with_precondition() ------------------------------------------

TEST(contract, with_precondition_invokes_wrapped) {
    auto f = with_precondition(
        [](int x) { return x * 2; },
        [](int x) { return x > 0; },
        "x > 0");
    EXPECT_EQ(10, f(5));
    EXPECT_EQ(0,  f(0));     // precondition fails (silent), but still runs
}

TEST(contract, with_precondition_returns_function_result) {
    auto f = with_precondition(
        [](int a, int b) { return a + b; },
        [](int a, int b) { return a >= 0 && b >= 0; },
        "a >= 0 && b >= 0");
    EXPECT_EQ(7, f(3, 4));
}

// ---- 3. with_strict_precondition() -----------------------------------

TEST(contract, with_strict_throws_on_failure) {
    auto f = with_strict_precondition(
        [](int) { return 0; },
        [](int x) { return x > 0; },
        "x > 0");
    bool threw = false;
    try { (void)f(-1); }
    catch (const ContractError&) { threw = true; }
    EXPECT_TRUE(threw);
}

TEST(contract, with_strict_returns_on_success) {
    auto f = with_strict_precondition(
        [](int x) { return x * 3; },
        [](int x) { return x > 0; },
        "x > 0");
    EXPECT_EQ(15, f(5));
}

TEST(contract, contract_error_message_includes_expr) {
    ContractError e("x > 0");
    std::string msg = e.what();
    EXPECT_TRUE(msg.find("x > 0") != std::string::npos);
}

// ---- 4. ContractStats ------------------------------------------------

TEST(contract, stats_increment_on_strict_failure) {
    reset_contract_stats();
    auto f = with_strict_precondition(
        [](int) { return 0; },
        [](int x) { return x > 0; },
        "x > 0");
    try { (void)f(-1); } catch (...) {}
    EXPECT_EQ(1u, ContractStats::instance().precondition_failures);
}

TEST(contract, stats_do_not_increment_on_success) {
    reset_contract_stats();
    auto f = with_strict_precondition(
        [](int x) { return x; },
        [](int x) { return x > 0; },
        "x > 0");
    (void)f(5);
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(contract, reset_clears_counters) {
    ContractStats::instance().precondition_failures = 42;
    reset_contract_stats();
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

// ---- 5. Macro expansion (compile-only test) --------------------------

TEST(contract, neuro_macros_expand) {
    // If the macros don't expand, this fails to compile.
    auto f = NEURO_EXPECTS(true) [](){ return 1; };
    (void)f;
    auto g = NEURO_ENSURES(true) [](){ return 2; };
    (void)g;
    // Just verify they compile; runtime behaviour is a no-op.
    EXPECT_EQ(0, 0);
}

RUN_ALL_TESTS()