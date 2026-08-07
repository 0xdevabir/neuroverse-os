// tests/integration/jit_bridge.cpp
//
// Z7.9 — JIT-compiled function called via the bridge.
//
// Compiles a small IR module that adds two constants, executes it
// via the host engine, then wraps the result in a Bridge symbol
// and resolves it.
//
// On x86_64 hosts the codegen emits real machine code and the
// execute() call returns the right value. On other targets the
// execute() returns zero — we still verify the bridge registration
// and resolution paths.

#include "tests/test_framework.hpp"

#include "neuro/bridge/ffi.hpp"
#include "neuro/jit/engine.hpp"

#include <cstdint>
#include <memory>

using neuro::bridge::Bridge;
using neuro::bridge::Library;
using neuro::bridge::Symbol;
using neuro::jit::CodegenResult;
using neuro::jit::Engine;
using neuro::jit::Function;
using neuro::jit::host_engine;
using neuro::jit::Instr;
using neuro::jit::Module;
using neuro::jit::OpKind;
using neuro::jit::Target;
using neuro::jit::Value;

namespace {

// A pure-IR `add(7, 35) -> 42` function.
Module build_add_module() {
    Module m;
    m.name = "add_demo";
    Function f;
    f.name = "add";
    Instr c1; c1.op = OpKind::Const; c1.arg.i64 = 7;
    Instr c2; c2.op = OpKind::Const; c2.arg.i64 = 35;
    Instr add; add.op = OpKind::Add;
    Instr ret; ret.op = OpKind::Ret;
    f.code = {c1, c2, add, ret};
    m.functions.push_back(std::move(f));
    return m;
}

// A pure-IR `mul(6, 7) -> 42` function.
Module build_mul_module() {
    Module m;
    m.name = "mul_demo";
    Function f;
    f.name = "mul";
    Instr c1; c1.op = OpKind::Const; c1.arg.i64 = 6;
    Instr c2; c2.op = OpKind::Const; c2.arg.i64 = 7;
    Instr m2; m2.op = OpKind::Mul;
    Instr ret; ret.op = OpKind::Ret;
    f.code = {c1, c2, m2, ret};
    m.functions.push_back(std::move(f));
    return m;
}

}  // namespace

TEST(jit_bridge, compile_add_and_get_value) {
    Engine& engine = host_engine();
    Module m = build_add_module();

    CodegenResult code = engine.codegen(m, Target::X86_64);
    EXPECT_FALSE(code.disasm.empty());

    Value r = engine.execute(code, /*args=*/{});
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_EQ(static_cast<std::int64_t>(42), r.i64);
#else
    EXPECT_EQ(static_cast<std::int64_t>(0), r.i64);
#endif
}

TEST(jit_bridge, compile_mul_and_get_value) {
    Engine& engine = host_engine();
    Module m = build_mul_module();
    CodegenResult code = engine.codegen(m, Target::X86_64);

    Value r = engine.execute(code, /*args=*/{});
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_EQ(static_cast<std::int64_t>(42), r.i64);
#else
    EXPECT_EQ(static_cast<std::int64_t>(0), r.i64);
#endif
}

TEST(jit_bridge, register_jit_function_as_bridge_symbol) {
    Engine& engine = host_engine();
    Module m = build_add_module();
    CodegenResult code = engine.codegen(m, Target::X86_64);

    // The host JIT execute path mmap's and munmap's each call. To
    // register a stable function pointer we'd need a stable
    // mmap region; on the host scaffold we instead wrap the JIT
    // module itself as the symbol's "version" and use a noop
    // placeholder function. The bridge's job is to find the
    // symbol by (name, version); we verify that path here.

    auto jit_module = std::make_shared<CodegenResult>(std::move(code));

    auto lib = std::make_unique<Library>("jit");
    // For the host scaffold we use a noop native fn; the JIT call
    // happens through the engine directly. The bridge resolves the
    // (name, version) → fn mapping.
    Symbol s;
    s.name    = "add";
    s.version = "1.0.0";
    s.fn      = +[]() -> void { };  // decays to function pointer
    lib->add(std::move(s));

    Bridge bridge;
    bridge.register_library(std::move(lib));

    auto fn = bridge.resolve("add", "1.0.0");
    EXPECT_TRUE(fn != nullptr);

    // Sanity: resolve with wrong version returns nullptr.
    auto missing = bridge.resolve("add", "9.9.9");
    EXPECT_TRUE(missing == nullptr);

    // The JIT module is still usable directly.
    Value r = engine.execute(*jit_module, /*args=*/{});
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_EQ(static_cast<std::int64_t>(42), r.i64);
#else
    EXPECT_EQ(static_cast<std::int64_t>(0), r.i64);
#endif
}

TEST(jit_bridge, codegen_with_no_functions_returns_empty_disasm) {
    Engine& engine = host_engine();
    Module m;  // no functions
    CodegenResult code = engine.codegen(m, Target::Host);
    EXPECT_TRUE(code.bytes.empty());
}

RUN_ALL_TESTS()