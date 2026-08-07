// tests/unit/bridge/ffi_test.cpp
//
// Tests for the NeuroBridge FFI lookup. The host scaffold keeps
// an in-process symbol table; Phase 1 swaps this for dlopen / PE
// / Mach-O. We test:
//   - register_library + Library::find() round trip
//   - Bridge::resolve() across multiple libraries
//   - version mismatch returns nullptr
//   - unknown symbol returns nullptr
//   - host_bridge() is a singleton
//   - calling a resolved NativeFn actually executes the function

#include "neuro/bridge/ffi.hpp"

#include <cstdint>
#include <string>

#include "../../test_framework.hpp"

using neuro::bridge::Bridge;
using neuro::bridge::Library;
using neuro::bridge::NativeFn;
using neuro::bridge::Symbol;
using neuro::bridge::host_bridge;

namespace {

// Test functions: each returns a stable int via the NativeFn ABI
// (a `void(*)()` that returns void). We cast to int-returning
// when calling so the test can read the result.
extern "C" std::int64_t bridge_add(std::int64_t a, std::int64_t b) {
    return a + b;
}
extern "C" std::int64_t bridge_sub(std::int64_t a, std::int64_t b) {
    return a - b;
}

using I64Fn = std::int64_t (*)(std::int64_t, std::int64_t);

}  // namespace

// ---- 1. Library find() -------------------------------------------------

TEST(bridge, library_find_returns_registered_symbol) {
    auto lib = std::make_unique<Library>("libmath");
    Symbol s;
    s.name    = "add";
    s.version = "1.0";
    s.fn      = reinterpret_cast<NativeFn>(&bridge_add);
    lib->add(s);
    auto* found = lib->find("add", "1.0");
    EXPECT_TRUE(found != nullptr);
    EXPECT_EQ("add", found->name);
}

TEST(bridge, library_find_version_mismatch) {
    auto lib = std::make_unique<Library>("libmath");
    Symbol s;
    s.name    = "add";
    s.version = "1.0";
    s.fn      = reinterpret_cast<NativeFn>(&bridge_add);
    lib->add(s);
    EXPECT_TRUE(lib->find("add", "1.0") != nullptr);
    EXPECT_TRUE(lib->find("add", "2.0") == nullptr);  // wrong version
    EXPECT_TRUE(lib->find("sub", "1.0") == nullptr);  // wrong name
}

// ---- 2. Bridge::resolve() ---------------------------------------------

TEST(bridge, resolve_searches_across_libraries) {
    Bridge b;
    {
        auto lib = std::make_unique<Library>("libmath");
        Symbol s; s.name = "add"; s.version = "1.0";
        s.fn = reinterpret_cast<NativeFn>(&bridge_add);
        lib->add(s);
        b.register_library(std::move(lib));
    }
    {
        auto lib = std::make_unique<Library>("libmore");
        Symbol s; s.name = "sub"; s.version = "1.0";
        s.fn = reinterpret_cast<NativeFn>(&bridge_sub);
        lib->add(s);
        b.register_library(std::move(lib));
    }
    EXPECT_TRUE(b.resolve("add", "1.0") != nullptr);
    EXPECT_TRUE(b.resolve("sub", "1.0") != nullptr);
    EXPECT_TRUE(b.resolve("mul", "1.0") == nullptr);
}

TEST(bridge, resolve_unknown_returns_null) {
    Bridge b;
    EXPECT_TRUE(b.resolve("nothing", "1.0") == nullptr);
}

// ---- 3. Calling a resolved symbol ------------------------------------

TEST(bridge, resolved_symbol_is_callable) {
    Bridge b;
    auto lib = std::make_unique<Library>("libmath");
    Symbol s; s.name = "add"; s.version = "1.0";
    s.fn = reinterpret_cast<NativeFn>(&bridge_add);
    lib->add(s);
    b.register_library(std::move(lib));
    NativeFn raw = b.resolve("add", "1.0");
    EXPECT_TRUE(raw != nullptr);
    auto typed = reinterpret_cast<I64Fn>(raw);
    EXPECT_EQ(7,  typed(3, 4));
    EXPECT_EQ(0,  typed(0, 0));
    EXPECT_EQ(-3, typed(-1, -2));
}

// ---- 4. Singleton -----------------------------------------------------

TEST(bridge, host_bridge_is_singleton) {
    auto& a = host_bridge();
    auto& b = host_bridge();
    EXPECT_EQ(&a, &b);
}

RUN_ALL_TESTS()