// neuro/jit/engine.hpp
//
// JIT engine (NeuroJIT, README §4.13).
//
// Per README §4.13 the JIT compiles hot DSL fragments and learned
// kernels down to native code at runtime. The compilation pipeline
// is: frontend IR → mid-end (opt passes) → backend (machine code).
//
// On the host we provide:
//   - The Module / Function / Instr IR.
//   - A textual disassembly backend (always works; prints IR).
//   - An x86_64 machine-code backend that emits real bytes for a
//     stack-based subset of the IR (Const / Add / Sub / Mul /
//     Load / Store / Ret) and executes them via an mmap'd
//     executable page.
//   - Pass infrastructure that runs user-supplied optimization
//     passes over the IR.
//
// The x86_64 backend is intended for the host test harness — the
// real backend targeting the kernel's executable-format subsystem
// lands in Phase 1. The IR shape doesn't have to change when that
// happens.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace neuro::jit {

// Target architecture for codegen. The host stub only emits a
// printable representation of the IR; the real backends (x86_64,
// aarch64, riscv64) land in Phase 1.
enum class Target : std::uint8_t {
    Host,
    X86_64,
    AArch64,
    RiscV64,
};

// One instruction in the IR. Real instructions land with the
// real IR + backend in Phase 1. The skeleton keeps a textual name
// + a small stack of typed operands.
enum class OpKind : std::uint8_t {
    Const,    // push immediate
    Add,      // pop a, pop b, push a + b
    Sub,
    Mul,
    Load,     // pop addr, push memory[addr]
    Store,    // pop addr, pop value, memory[addr] = value
    Ret,      // halt + return top
    Call,     // call named function
};

struct Value {
    enum class Kind : std::uint8_t { I64, F64 };
    Kind        kind = Kind::I64;
    std::int64_t i64 = 0;
    double        f64 = 0.0;
};

struct Instr {
    OpKind   op = OpKind::Const;
    Value    arg;
    std::string callee;        // for OpKind::Call
};

struct Function {
    std::string            name;
    std::vector<Instr>     code;
};

struct Module {
    std::string            name;
    std::vector<Function>  functions;
};

// Optimization pass: pure function over the module's IR. Returns
// true if the IR was modified.
using Pass = std::function<bool(Module&)>;

// Codegen result: a printable representation of the produced
// machine code. The real engine returns an mmap'd executable page.
struct CodegenResult {
    Target                       target = Target::Host;
    std::vector<std::byte>       bytes;
    std::string                  disasm;  // textual disassembly
};

// Engine trait. Implementations produce CodegenResults for Modules.
class Engine {
public:
    Engine()                    = default;
    Engine(const Engine&)       = delete;
    Engine& operator=(const Engine&) = delete;
    virtual ~Engine()           = default;

    // Add an optimization pass.
    void add_pass(Pass p) { passes_.push_back(std::move(p)); }

    // Run every registered pass over the module.
    bool optimize(Module& m) {
        bool changed = false;
        for (auto& p : passes_) changed |= p(m);
        return changed;
    }

    // Lower the module to machine code for `t`. The host stub emits
    // a textual disassembly only; real codegen lands in Phase 1.
    [[nodiscard]] virtual CodegenResult
        codegen(const Module& m, Target t) const = 0;

    // Execute a previously-codegen'd function. The stub returns
    // an empty result; real execution lands in Phase 1.
    [[nodiscard]] virtual Value
        execute(const CodegenResult& code,
                std::span<const Value> args) const = 0;

private:
    std::vector<Pass> passes_;
};

// Singleton factory: one Engine per process.
Engine& host_engine();

}  // namespace neuro::jit