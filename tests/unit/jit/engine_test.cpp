// tests/unit/jit/engine_test.cpp
//
// End-to-end smoke test for the JIT engine + x86_64 backend.
//
// Exercises the full pipeline that the rest of the system will
// eventually use: build a Module in the IR, run it through the
// engine, and check the produced CodegenResult.
//
// On x86_64 hosts the test also calls execute() and verifies the
// returned integer matches what the IR would compute. On non-x86_64
// hosts execute() returns zero (no-op) so we only check the bytes
// and disassembly.

#include "neuro/jit/engine.hpp"
#include "neuro/jit/x86_64.hpp"

#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::jit::Engine;
using neuro::jit::Module;
using neuro::jit::Function;
using neuro::jit::Instr;
using neuro::jit::OpKind;
using neuro::jit::Target;
using neuro::jit::Value;
using neuro::jit::host_engine;

// Build a Module that computes 2 + 3 and returns it.
static Module make_add_module() {
    Module m;
    m.name = "smoke";

    Function fn;
    fn.name = "two_plus_three";

    Instr c2;  c2.op  = OpKind::Const;
    c2.arg     = Value{Value::Kind::I64, 2, 0.0};
    Instr c3;  c3.op  = OpKind::Const;
    c3.arg     = Value{Value::Kind::I64, 3, 0.0};
    Instr add; add.op = OpKind::Add;
    Instr ret; ret.op  = OpKind::Ret;
    ret.arg    = Value{Value::Kind::I64, 0, 0.0};

    fn.code = {c2, c3, add, ret};
    m.functions = {fn};
    return m;
}

// Build a Module that just returns the constant 42.
static Module make_const_module() {
    Module m;
    m.name = "const_smoke";

    Function fn;
    fn.name = "forty_two";

    Instr c;  c.op = OpKind::Const;
    c.arg     = Value{Value::Kind::I64, 42, 0.0};
    Instr r;  r.op = OpKind::Ret;
    r.arg     = Value{Value::Kind::I64, 0, 0.0};

    fn.code = {c, r};
    m.functions = {fn};
    return m;
}

// ---- 1. textual backend always works -----------------------------------

TEST(jit_engine, textual_disasm_lists_functions) {
    Engine& e = host_engine();
    Module m = make_add_module();
    auto r = e.codegen(m, Target::Host);
    EXPECT_EQ(Target::Host, r.target);
    EXPECT_TRUE(r.disasm.find("two_plus_three") != std::string::npos);
    EXPECT_TRUE(r.disasm.find("const 2") != std::string::npos);
    EXPECT_TRUE(r.disasm.find("add") != std::string::npos);
    EXPECT_TRUE(r.disasm.find("ret") != std::string::npos);
}

// ---- 2. x86_64 backend produces bytes + disasm -------------------------

TEST(jit_engine, x86_64_backend_emits_bytes_and_disasm) {
    Engine& e = host_engine();
    Module m = make_const_module();
    auto r = e.codegen(m, Target::X86_64);
    EXPECT_EQ(Target::X86_64, r.target);
    // 6A 2A 58 C3  C3 — push 42, pop rax, ret, defensive ret.
    // Just check we got something non-empty and the disasm mentions
    // the function name.
    EXPECT_FALSE(r.bytes.empty());
    EXPECT_TRUE(r.disasm.find("forty_two") != std::string::npos);
}

// ---- 3. x86_64 bytes for Const 42 use push imm8 -------------------------

TEST(jit_engine, x86_64_const_42_uses_push_imm8) {
    Engine& e = host_engine();
    Module m = make_const_module();
    auto r = e.codegen(m, Target::X86_64);
    // The first two bytes are `push imm8` (6A 2A) since 42 fits
    // in [-128, 127]. Even if the encoder evolves the exact byte
    // sequence, push imm8 is the form we picked for compactness.
    EXPECT_FALSE(r.bytes.empty());
    EXPECT_EQ(std::byte{0x6A}, r.bytes[0]);
    EXPECT_EQ(std::byte{0x2A}, r.bytes[1]);
}

// ---- 4. x86_64 backend rejects Call op with no bytes --------------------

TEST(jit_engine, x86_64_backend_unsupported_op_returns_empty_bytes) {
    Engine& e = host_engine();
    Module m;
    Function fn;
    fn.name = "with_call";
    Instr c; c.op = OpKind::Call; c.callee = "puts";
    fn.code = {c};
    m.functions = {fn};
    auto r = e.codegen(m, Target::X86_64);
    // x86_64 backend can't emit Call yet; the engine falls back to
    // textual disassembly with no bytes.
    EXPECT_TRUE(r.bytes.empty());
    EXPECT_FALSE(r.disasm.empty());
}

// ---- 5. execute() path on x86_64 only ----------------------------------

TEST(jit_engine, execute_const_42) {
    Engine& e = host_engine();
    Module m = make_const_module();
    auto r = e.codegen(m, Target::X86_64);
#if defined(__x86_64__) || defined(_M_X64)
    // Real path: mmap'd page executes and returns 42 in rax.
    Value v = e.execute(r, {});
    EXPECT_EQ(Value::Kind::I64, v.kind);
    EXPECT_EQ(42, v.i64);
#else
    // No-op fallback on non-x86_64 hosts: execute() returns {}.
    Value v = e.execute(r, {});
    EXPECT_EQ(Value::Kind::I64, v.kind);
    EXPECT_EQ(0, v.i64);
    (void)m; (void)e;
#endif
}

RUN_ALL_TESTS()