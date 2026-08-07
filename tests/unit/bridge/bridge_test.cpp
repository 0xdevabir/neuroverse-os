// tests/unit/bridge/bridge_test.cpp
//
// Direct unit tests for the Bridge / Library / Symbol trio defined
// in include/neuro/bridge/ffi.hpp.
//
// The existing ffi_test.cpp exercises the singleton host_bridge() and
// uses process-wide state; this file builds an isolated Bridge
// instance per test so we can verify the contract:
//
//   - Library: name(), add(), find() match, find() miss returns null
//   - Bridge: register_library(), resolve() hits / misses,
//     library_count() tracks inserts / replaces, resolve across
//     multiple libraries
//   - Symbol: name + version + fn fields
//   - Library is non-copyable
//   - ResolveError message includes the symbol name

#include "neuro/bridge/ffi.hpp"

#include <memory>
#include <string>
#include <type_traits>

#include "../../test_framework.hpp"

using neuro::bridge::Bridge;
using neuro::bridge::Library;
using neuro::bridge::NativeFn;
using neuro::bridge::ResolveError;
using neuro::bridge::Symbol;

namespace {

// A trivial native function we can use as a Symbol::fn.
void fn_a() {}
void fn_b() {}

}  // namespace

// ---- 1. Symbol field access -------------------------------------

TEST(bridge, symbol_field_round_trip) {
    Symbol s{};
    s.name    = "open";
    s.version = "1.0";
    s.fn      = &fn_a;
    EXPECT_EQ(std::string("open"), s.name);
    EXPECT_EQ(std::string("1.0"),  s.version);
    EXPECT_TRUE(s.fn != nullptr);
}

// ---- 2. Library name + add + find ------------------------------

TEST(bridge, library_name_round_trip) {
    Library lib("libc");
    EXPECT_EQ(std::string("libc"), lib.name());
}

TEST(bridge, library_find_matches_name_and_version) {
    Library lib("libfoo");
    Symbol s;
    s.name    = "open";
    s.version = "1.0";
    s.fn      = &fn_a;
    lib.add(s);
    auto* found = lib.find("open", "1.0");
    EXPECT_TRUE(found != nullptr);
    EXPECT_EQ(s.fn, found->fn);
}

TEST(bridge, library_find_misses_wrong_version) {
    Library lib("libfoo");
    Symbol s;
    s.name = "open";
    s.version = "1.0";
    s.fn = &fn_a;
    lib.add(s);
    EXPECT_TRUE(lib.find("open", "2.0") == nullptr);
}

TEST(bridge, library_find_misses_wrong_name) {
    Library lib("libfoo");
    Symbol s;
    s.name = "open";
    s.version = "1.0";
    s.fn = &fn_a;
    lib.add(s);
    EXPECT_TRUE(lib.find("close", "1.0") == nullptr);
}

TEST(bridge, library_find_empty_returns_null) {
    Library lib("libfoo");
    EXPECT_TRUE(lib.find("anything", "1.0") == nullptr);
}

TEST(bridge, library_non_copyable) {
    static_assert(!std::is_copy_constructible_v<Library>);
    static_assert(!std::is_copy_assignable_v<Library>);
}

// ---- 3. Bridge: register + resolve -----------------------------

TEST(bridge, bridge_register_and_resolve) {
    Bridge b;
    auto lib = std::make_unique<Library>("libfoo");
    Symbol s;
    s.name    = "puts";
    s.version = "1.0";
    s.fn      = &fn_a;
    lib->add(s);
    b.register_library(std::move(lib));

    EXPECT_EQ(static_cast<std::size_t>(1), b.library_count());
    auto fn = b.resolve("puts", "1.0");
    EXPECT_TRUE(fn == &fn_a);
}

TEST(bridge, bridge_resolve_unknown_returns_null) {
    Bridge b;
    EXPECT_TRUE(b.resolve("nowhere", "1.0") == nullptr);
}

TEST(bridge, bridge_resolves_across_multiple_libraries) {
    Bridge b;
    {
        auto l = std::make_unique<Library>("liba");
        Symbol s;
        s.name = "alpha";
        s.version = "1.0";
        s.fn = &fn_a;
        l->add(s);
        b.register_library(std::move(l));
    }
    {
        auto l = std::make_unique<Library>("libb");
        Symbol s;
        s.name = "beta";
        s.version = "1.0";
        s.fn = &fn_b;
        l->add(s);
        b.register_library(std::move(l));
    }
    EXPECT_EQ(static_cast<std::size_t>(2), b.library_count());
    EXPECT_TRUE(b.resolve("alpha", "1.0") == &fn_a);
    EXPECT_TRUE(b.resolve("beta",  "1.0") == &fn_b);
}

TEST(bridge, bridge_register_replaces_existing) {
    Bridge b;
    {
        auto l = std::make_unique<Library>("libfoo");
        Symbol s;
        s.name = "open";
        s.version = "1.0";
        s.fn = &fn_a;
        l->add(s);
        b.register_library(std::move(l));
    }
    EXPECT_EQ(static_cast<std::size_t>(1), b.library_count());
    {
        auto l = std::make_unique<Library>("libfoo");
        Symbol s;
        s.name = "open";
        s.version = "1.0";
        s.fn = &fn_b;  // different fn pointer
        l->add(s);
        b.register_library(std::move(l));
    }
    EXPECT_EQ(static_cast<std::size_t>(1), b.library_count());  // still 1
    EXPECT_TRUE(b.resolve("open", "1.0") == &fn_b);
}

TEST(bridge, bridge_library_count_zero_by_default) {
    Bridge b;
    EXPECT_EQ(static_cast<std::size_t>(0), b.library_count());
}

TEST(bridge, bridge_non_copyable) {
    static_assert(!std::is_copy_constructible_v<Bridge>);
    static_assert(!std::is_copy_assignable_v<Bridge>);
}

// ---- 4. ResolveError message -----------------------------------

TEST(bridge, resolve_error_includes_symbol_name) {
    ResolveError e("open");
    std::string msg = e.what();
    EXPECT_TRUE(msg.find("open") != std::string::npos);
    EXPECT_TRUE(msg.find("bridge") != std::string::npos);
}

// ---- 5. many libraries + many symbols per library --------------

TEST(bridge, many_symbols_in_one_library) {
    Bridge b;
    auto l = std::make_unique<Library>("big");
    for (int i = 0; i < 50; ++i) {
        Symbol s;
        s.name = "sym_" + std::to_string(i);
        s.version = "1.0";
        s.fn = &fn_a;
        l->add(s);
    }
    b.register_library(std::move(l));
    for (int i = 0; i < 50; ++i) {
        auto fn = b.resolve("sym_" + std::to_string(i), "1.0");
        EXPECT_TRUE(fn == &fn_a);
    }
    EXPECT_TRUE(b.resolve("sym_50", "1.0") == nullptr);
}

RUN_ALL_TESTS()