// tests/unit/jit/x86_64_test.cpp
//
// Tests for the JIT's x86_64 instruction encoder. Each test pins
// the exact bytes the host-scaffold backend should emit for a
// known instruction, so any change to the encoding can be caught
// before it lands.
//
// On non-x86_64 hosts the encoder functions are no-ops (returning
// false / emitting nothing), and these tests are skipped via
// platform-gating at the bottom of the file. The encoder still
// compiles so the test files don't #ifdef away.

#include "neuro/jit/x86_64.hpp"
#include "neuro/jit/engine.hpp"

#include <cstring>
#include <span>
#include <vector>

#include "../../test_framework.hpp"

using neuro::jit::OpKind;
using neuro::jit::Instr;
using neuro::jit::Function;
using neuro::jit::Value;
using neuro::jit::x86_64::Reg;
using neuro::jit::x86_64::emit_mov_reg_imm64;
using neuro::jit::x86_64::emit_push_reg;
using neuro::jit::x86_64::emit_pop_reg;
using neuro::jit::x86_64::emit_add_rax_rbx;
using neuro::jit::x86_64::emit_sub_rax_rbx;
using neuro::jit::x86_64::emit_imul_rax_rbx;
using neuro::jit::x86_64::emit_store_rax_rbx;
using neuro::jit::x86_64::emit_load_rax;
using neuro::jit::x86_64::emit_ret;
using neuro::jit::x86_64::emit_instr;
using neuro::jit::x86_64::emit_function;

// ---- Hex helper ---------------------------------------------------------

static std::vector<std::uint8_t> hex_to_bytes(std::string_view s) {
    auto nib = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    std::vector<std::uint8_t> v;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        v.push_back((nib(s[i]) << 4) | nib(s[i + 1]));
    }
    return v;
}

static std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& in) {
    std::vector<std::byte> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = static_cast<std::byte>(in[i]);
    }
    return out;
}

// The encoder emits x86_64 bytes. That encoding is deterministic
// regardless of the host the test runs on — we test the byte
// sequence in isolation. The execute() path (engine.cpp) is what
// gates on platform.

// ---- 1. mov rax, imm64 --------------------------------------------------

TEST(x86_64, mov_rax_imm64) {
    std::vector<std::byte> out;
    emit_mov_reg_imm64(out, Reg::Rax, 0xCAFEBABEDEADBEEFULL);
    // 48 B8 <imm64 little-endian>
    // 0xCAFEBABEDEADBEEF LE bytes = ef be ad de be ba fe ca
    auto expected = as_bytes(hex_to_bytes(
        "48" "b8" "efbe" "adde" "beba" "feca"));
    EXPECT_EQ(expected.size(), out.size());
    EXPECT_EQ(0, std::memcmp(expected.data(), out.data(), expected.size()));
}

TEST(x86_64, mov_rbx_imm64) {
    std::vector<std::byte> out;
    emit_mov_reg_imm64(out, Reg::Rbx, 0x1122334455667788ULL);
    // 48 BB 88 77 66 55 44 33 22 11
    auto expected = as_bytes(hex_to_bytes(
        "48bb88776655443322 11"));
    // Re-build expected with proper length (no space).
    expected = as_bytes(hex_to_bytes("48bb8877665544332211"));
    EXPECT_EQ(expected.size(), out.size());
    EXPECT_EQ(0, std::memcmp(expected.data(), out.data(), expected.size()));
}

// ---- 2. push / pop ------------------------------------------------------

TEST(x86_64, push_pop_rax) {
    std::vector<std::byte> out;
    emit_push_reg(out, Reg::Rax);
    emit_pop_reg(out, Reg::Rax);
    // push rax (50) + pop rax (58)
    auto expected = as_bytes(hex_to_bytes("5058"));
    EXPECT_EQ(expected.size(), out.size());
    EXPECT_EQ(0, std::memcmp(expected.data(), out.data(), expected.size()));
}

TEST(x86_64, push_pop_rbx) {
    std::vector<std::byte> out;
    emit_push_reg(out, Reg::Rbx);
    emit_pop_reg(out, Reg::Rbx);
    // push rbx (53) + pop rbx (5B)
    auto expected = as_bytes(hex_to_bytes("535b"));
    EXPECT_EQ(2u, out.size());
    EXPECT_EQ(std::byte{0x53}, out[0]);
    EXPECT_EQ(std::byte{0x5b}, out[1]);
}

// ---- 3. add / sub / imul ------------------------------------------------

TEST(x86_64, add_rax_rbx) {
    std::vector<std::byte> out;
    emit_add_rax_rbx(out);
    auto expected = as_bytes(hex_to_bytes("4801d8"));
    EXPECT_EQ(expected, out);
}

TEST(x86_64, sub_rax_rbx) {
    std::vector<std::byte> out;
    emit_sub_rax_rbx(out);
    auto expected = as_bytes(hex_to_bytes("4829d8"));
    EXPECT_EQ(expected, out);
}

TEST(x86_64, imul_rax_rbx) {
    std::vector<std::byte> out;
    emit_imul_rax_rbx(out);
    auto expected = as_bytes(hex_to_bytes("480fafc3"));
    EXPECT_EQ(expected, out);
}

// ---- 4. load / store ----------------------------------------------------

TEST(x86_64, store_rax_rbx) {
    std::vector<std::byte> out;
    emit_store_rax_rbx(out);
    auto expected = as_bytes(hex_to_bytes("488918"));
    EXPECT_EQ(expected, out);
}

TEST(x86_64, load_rax) {
    std::vector<std::byte> out;
    emit_load_rax(out);
    auto expected = as_bytes(hex_to_bytes("488b00"));
    EXPECT_EQ(expected, out);
}

// ---- 5. ret ------------------------------------------------------------

TEST(x86_64, ret_encoding) {
    std::vector<std::byte> out;
    emit_ret(out);
    EXPECT_EQ(1u, out.size());
    EXPECT_EQ(std::byte{0xC3}, out[0]);
}

// ---- 6. emit_instr: Const small immediate (push imm8) -------------------

TEST(x86_64, const_small_uses_push_imm8) {
    Instr ins;
    ins.op  = OpKind::Const;
    ins.arg = Value{Value::Kind::I64, 7, 0.0};
    std::vector<std::byte> out;
    EXPECT_TRUE(emit_instr(out, ins));
    // 6A 07 = push 7 (2 bytes)
    auto expected = as_bytes(hex_to_bytes("6a07"));
    EXPECT_EQ(expected, out);
}

// ---- 7. emit_instr: Const large immediate (push imm64) ------------------

TEST(x86_64, const_large_uses_mov_push_imm64) {
    Instr ins;
    ins.op  = OpKind::Const;
    ins.arg = Value{Value::Kind::I64, 0x1234567890ABCDEFLL, 0.0};
    std::vector<std::byte> out;
    EXPECT_TRUE(emit_instr(out, ins));
    // 48 B8 EF CD AB 90 78 56 34 12  50
    auto expected = as_bytes(hex_to_bytes(
        "48b8efcdab907856341250"));
    EXPECT_EQ(expected, out);
}

// ---- 8. emit_function: const + add + ret -------------------------------

TEST(x86_64, function_const_add_ret) {
    Function fn;
    fn.name = "add_2_3";
    Instr a; a.op = OpKind::Const; a.arg = Value{Value::Kind::I64, 2, 0.0};
    Instr b; b.op = OpKind::Const; b.arg = Value{Value::Kind::I64, 3, 0.0};
    Instr c; c.op = OpKind::Add;
    Instr r; r.op = OpKind::Ret;  r.arg = Value{Value::Kind::I64, 0, 0.0};
    fn.code = {a, b, c, r};

    std::vector<std::byte> out;
    EXPECT_TRUE(emit_function(out, fn));
    // 6A 02  (push 2)
    // 6A 03  (push 3)
    // 58 5B 48 01 D8 50  (pop rax; pop rbx; add; push rax)
    // 58 C3  (pop rax; ret — from the explicit Ret)
    // (emit_function appends an extra ret defensively, so we get 2x C3.)
    auto expected = as_bytes(hex_to_bytes(
        "6a02"                    // push 2
        "6a03"                    // push 3
        "585b4801d850"            // pop rax; pop rbx; add; push rax
        "58c3"                    // pop rax; ret (the user-supplied one)
        "c3"));                   // the defensive ret
    EXPECT_EQ(expected.size(), out.size());
    EXPECT_EQ(0, std::memcmp(expected.data(), out.data(), expected.size()));
}

// ---- 9. emit_function: unsupported op returns false --------------------

TEST(x86_64, function_with_call_fails) {
    Function fn;
    fn.name = "calls_other";
    Instr i; i.op = OpKind::Call; i.callee = "puts";
    fn.code = {i};
    std::vector<std::byte> out;
    EXPECT_FALSE(emit_function(out, fn));
}

RUN_ALL_TESTS()