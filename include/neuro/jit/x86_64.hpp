// neuro/jit/x86_64.hpp
//
// Minimal x86_64 instruction emitter used by the host-scaffold JIT.
//
// The emitter supports the subset of x86_64 needed to lower the
// stack-based Module / Function / Instr IR defined in engine.hpp:
//
//   push/pop rax, rbx, rcx, rdx   (used as a 4-deep eval stack)
//   mov rax, imm64
//   mov [rax], rbx / mov rax, [rax]
//   add/sub/imul rax, rbx
//   ret
//
// Every public function appends the encoded bytes to an
// std::vector<std::byte>. All methods are pure (no global state) so
// the encoder is safe to use from multiple compilation units.
//
// This is NOT a full x86_64 assembler — it's just enough to
// demonstrate the JIT round-trip on the host. The real backend
// landing with Phase 1 will use a proper assembler (cranelift /
// asmjit / similar) with register allocation, instruction
// selection, and full instruction coverage.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "neuro/jit/engine.hpp"   // OpKind, Value, Instr

namespace neuro::jit::x86_64 {

// Register IDs. We only need a handful — rax / rbx / rcx / rdx —
// for the stack-based encoding. rsp/rbp are reserved (stack frame
// management isn't modelled in this minimal subset).
enum class Reg : std::uint8_t {
    Rax = 0,
    Rbx = 3,
    Rcx = 1,
    Rdx = 2,
};

// Emit `mov reg, imm64` (REX.W + B8+rd imm64). 10 bytes.
void emit_mov_reg_imm64(std::vector<std::byte>& out, Reg r,
                        std::int64_t imm) noexcept;

// Emit `push reg` (50+rd). 1 byte.
void emit_push_reg(std::vector<std::byte>& out, Reg r) noexcept;

// Emit `pop reg` (58+rd). 1 byte.
void emit_pop_reg(std::vector<std::byte>& out, Reg r) noexcept;

// Emit `add rax, rbx` (48 01 D8). 3 bytes.
void emit_add_rax_rbx(std::vector<std::byte>& out) noexcept;

// Emit `sub rax, rbx` (48 29 D8). 3 bytes.
void emit_sub_rax_rbx(std::vector<std::byte>& out) noexcept;

// Emit `imul rax, rbx` (48 0F AF C3). 4 bytes.
void emit_imul_rax_rbx(std::vector<std::byte>& out) noexcept;

// Emit `mov [rax], rbx` (48 89 18). 3 bytes.
void emit_store_rax_rbx(std::vector<std::byte>& out) noexcept;

// Emit `mov rax, [rax]` (48 8B 00). 3 bytes.
void emit_load_rax(std::vector<std::byte>& out) noexcept;

// Emit `ret` (C3). 1 byte.
void emit_ret(std::vector<std::byte>& out) noexcept;

// Lower a single Instr to bytes. Returns false for any op the
// host scaffold doesn't implement yet.
bool emit_instr(std::vector<std::byte>& out, const Instr& ins) noexcept;

// Lower an entire function to bytes. Each function ends with a
// `ret`. Returns false (and may append a partial function) if any
// instruction in the function is unsupported.
bool emit_function(std::vector<std::byte>& out,
                   const Function& fn) noexcept;

}  // namespace neuro::jit::x86_64