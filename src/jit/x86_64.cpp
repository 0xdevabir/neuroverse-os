// src/jit/x86_64.cpp
//
// Minimal x86_64 instruction emitter implementation (see the header
// for the supported subset).
//
// The encoder itself produces x86_64 *bytes* — the encoding is just
// a sequence of std::byte values and doesn't depend on what host
// we're compiling on. The platform-specific part is executing the
// emitted code (engine.cpp's mmap + cast); the encoder is portable.

#include "neuro/jit/x86_64.hpp"

#include <cstring>

namespace neuro::jit::x86_64 {

namespace {

inline void emit_byte(std::vector<std::byte>& out, std::uint8_t b) {
    out.push_back(static_cast<std::byte>(b));
}

inline void emit_u64_le(std::vector<std::byte>& out, std::int64_t v) {
    std::uint64_t bits = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>(bits & 0xFF));
        bits >>= 8;
    }
}

inline std::uint8_t reg_low3(Reg r) {
    return static_cast<std::uint8_t>(r) & 0x7;
}

}  // namespace

void emit_mov_reg_imm64(std::vector<std::byte>& out, Reg r,
                        std::int64_t imm) noexcept {
    // REX.W (48) + B8+rd opcode + imm64. REX.R is set if the
    // register is r8-r15; we only use rax-rbx-rcx-rdx so REX.R is
    // always 0.
    emit_byte(out, 0x48);
    emit_byte(out, static_cast<std::uint8_t>(0xB8 + reg_low3(r)));
    emit_u64_le(out, imm);
}

void emit_push_reg(std::vector<std::byte>& out, Reg r) noexcept {
    emit_byte(out, static_cast<std::uint8_t>(0x50 + reg_low3(r)));
}

void emit_pop_reg(std::vector<std::byte>& out, Reg r) noexcept {
    emit_byte(out, static_cast<std::uint8_t>(0x58 + reg_low3(r)));
}

void emit_add_rax_rbx(std::vector<std::byte>& out) noexcept {
    // 48 01 D8 — REX.W + ADD r/m64,r64 with ModR/M = 11 011 000 (rax,rbx)
    emit_byte(out, 0x48);
    emit_byte(out, 0x01);
    emit_byte(out, 0xD8);
}

void emit_sub_rax_rbx(std::vector<std::byte>& out) noexcept {
    // 48 29 D8 — REX.W + SUB r/m64,r64 with ModR/M = 11 011 000
    emit_byte(out, 0x48);
    emit_byte(out, 0x29);
    emit_byte(out, 0xD8);
}

void emit_imul_rax_rbx(std::vector<std::byte>& out) noexcept {
    // 48 0F AF C3 — REX.W + IMUL r64, r/m64 with ModR/M = 11 000 011
    emit_byte(out, 0x48);
    emit_byte(out, 0x0F);
    emit_byte(out, 0xAF);
    emit_byte(out, 0xC3);
}

void emit_store_rax_rbx(std::vector<std::byte>& out) noexcept {
    // 48 89 18 — REX.W + MOV r/m64, r64 with ModR/M = 11 011 000 (rax)
    emit_byte(out, 0x48);
    emit_byte(out, 0x89);
    emit_byte(out, 0x18);
}

void emit_load_rax(std::vector<std::byte>& out) noexcept {
    // 48 8B 00 — REX.W + MOV r64, r/m64 with ModR/M = 11 000 000 (rax)
    emit_byte(out, 0x48);
    emit_byte(out, 0x8B);
    emit_byte(out, 0x00);
}

void emit_ret(std::vector<std::byte>& out) noexcept {
    emit_byte(out, 0xC3);
}

bool emit_instr(std::vector<std::byte>& out, const Instr& ins) noexcept {
    switch (ins.op) {
        case OpKind::Const: {
            std::int64_t v = ins.arg.kind == Value::Kind::I64
                               ? ins.arg.i64
                               : static_cast<std::int64_t>(ins.arg.f64);
            // Cheap constant folding: if value fits in imm8 with
            // sign-extension, use `push imm8` (2 bytes) instead of
            // the 10-byte imm64 form. This isn't a real peephole
            // — it's just a clearer trace.
            if (v >= -128 && v <= 127) {
                emit_byte(out, 0x6A);              // push imm8
                emit_byte(out, static_cast<std::uint8_t>(
                                    static_cast<std::int8_t>(v)));
            } else {
                emit_mov_reg_imm64(out, Reg::Rax, v);
                emit_push_reg(out, Reg::Rax);
            }
            return true;
        }
        case OpKind::Add:
            emit_pop_reg(out, Reg::Rax);
            emit_pop_reg(out, Reg::Rbx);
            emit_add_rax_rbx(out);
            emit_push_reg(out, Reg::Rax);
            return true;
        case OpKind::Sub:
            emit_pop_reg(out, Reg::Rax);
            emit_pop_reg(out, Reg::Rbx);
            emit_sub_rax_rbx(out);
            emit_push_reg(out, Reg::Rax);
            return true;
        case OpKind::Mul:
            emit_pop_reg(out, Reg::Rax);
            emit_pop_reg(out, Reg::Rbx);
            emit_imul_rax_rbx(out);
            emit_push_reg(out, Reg::Rax);
            return true;
        case OpKind::Load:
            emit_pop_reg(out, Reg::Rax);
            emit_load_rax(out);
            emit_push_reg(out, Reg::Rax);
            return true;
        case OpKind::Store:
            emit_pop_reg(out, Reg::Rax);  // value (push second)
            emit_pop_reg(out, Reg::Rbx);  // address (push first)
            emit_store_rax_rbx(out);
            return true;
        case OpKind::Ret:
            emit_pop_reg(out, Reg::Rax);
            emit_ret(out);
            return true;
        case OpKind::Call:
            // No host-side support for calling out to arbitrary
            // addresses. Phase 1's backend models this properly.
            return false;
    }
    return false;
}

bool emit_function(std::vector<std::byte>& out,
                   const Function& fn) noexcept {
    for (const auto& ins : fn.code) {
        if (!emit_instr(out, ins)) return false;
    }
    // Defensive ret in case the function forgot one.
    emit_ret(out);
    return true;
}

}  // namespace neuro::jit::x86_64