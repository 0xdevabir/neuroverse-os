// src/jit/engine.cpp
//
// JIT engine — host scaffold.
//
// On the host we provide:
//   - The textual IR backend: always works, prints disassembly.
//   - The x86_64 backend: emits real machine bytes for the
//     stack-based subset of the IR and executes them via an
//     mmap'd executable page. Gated by Target::X86_64 and only
//     built on x86_64 hosts (see src/jit/x86_64.cpp).
//
// The real backend targeting the kernel executable-format
// subsystem lands in Phase 1; the IR shape doesn't change.

#include "neuro/jit/engine.hpp"
#include "neuro/jit/x86_64.hpp"

#include <cstring>
#include <sstream>
#include <string>

#if defined(__x86_64__) || defined(_M_X64)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace neuro::jit {

namespace {

std::string_view op_name(OpKind op) {
    switch (op) {
        case OpKind::Const:  return "const";
        case OpKind::Add:    return "add";
        case OpKind::Sub:    return "sub";
        case OpKind::Mul:    return "mul";
        case OpKind::Load:   return "load";
        case OpKind::Store:  return "store";
        case OpKind::Ret:    return "ret";
        case OpKind::Call:   return "call";
    }
    return "?";
}

std::string format_arg(const Value& v) {
    std::ostringstream s;
    if (v.kind == Value::Kind::I64) s << v.i64;
    else                              s << v.f64;
    return s.str();
}

// ---- Textual disassembly backend ----------------------------------------

std::string textual_disasm(const Module& m) {
    std::ostringstream d;
    d << ";; module " << m.name << "\n";
    for (const auto& fn : m.functions) {
        d << "fn " << fn.name << ":\n";
        for (const auto& ins : fn.code) {
            d << "  " << op_name(ins.op);
            if (ins.op == OpKind::Const || ins.op == OpKind::Call) {
                d << " ";
                if (ins.op == OpKind::Call) d << ins.callee;
                else                        d << format_arg(ins.arg);
            }
            d << "\n";
        }
    }
    return d.str();
}

// ---- x86_64 backend -----------------------------------------------------
//
// emit_function returns false on an unsupported instruction. We
// surface that to the caller as a textual result (no bytes) so the
// rest of the pipeline can fall back.

bool emit_module_x86_64(const Module& m, CodegenResult& r) {
    r.target = Target::X86_64;
    for (const auto& fn : m.functions) {
        std::size_t before = r.bytes.size();
        if (!x86_64::emit_function(r.bytes, fn)) {
            // Roll back; signal failure by leaving bytes empty.
            r.bytes.resize(before);
            return false;
        }
    }
    return !r.bytes.empty();
}

// ---- Execution ----------------------------------------------------------
//
// On x86_64 hosts we mmap a single RWX page, copy the bytes, and
// cast the address to a function pointer. The first function in
// the module is invoked with no arguments; the returned rax is
// returned as a Value. On non-x86_64 hosts execute() returns zero.

class HostEngine : public Engine {
public:
    [[nodiscard]] CodegenResult
        codegen(const Module& m, Target t) const override {
        CodegenResult r;
        if (t == Target::X86_64) {
            if (emit_module_x86_64(m, r)) {
                // Still emit a textual disassembly so callers can
                // see what was compiled.
                r.disasm = textual_disasm(m);
                return r;
            }
            // Fall through to textual fallback.
        }
        r.target = t;
        r.disasm = textual_disasm(m);
        return r;
    }

    [[nodiscard]] Value
        execute(const CodegenResult& code,
                std::span<const Value> /*args*/) const override {
#if defined(__x86_64__) || defined(_M_X64)
        if (code.target != Target::X86_64 || code.bytes.empty()) {
            return Value{};
        }
        // Round up to page size for the mmap call.
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) page = 4096;
        std::size_t size = (code.bytes.size() + static_cast<std::size_t>(page) - 1)
                            & ~(static_cast<std::size_t>(page) - 1);
        if (size == 0) size = static_cast<std::size_t>(page);
        void* mem = mmap(nullptr, size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return Value{};
        std::memcpy(mem, code.bytes.data(), code.bytes.size());
        using Fn = std::int64_t (*)();
        Fn fn = reinterpret_cast<Fn>(mem);
        std::int64_t ret = fn();
        munmap(mem, size);
        return Value{Value::Kind::I64, ret, 0.0};
#else
        (void)code;
        return Value{};
#endif
    }
};

}  // namespace

Engine& host_engine() {
    static HostEngine e;
    return e;
}

}  // namespace neuro::jit