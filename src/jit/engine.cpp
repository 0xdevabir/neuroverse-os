// src/jit/engine.cpp
//
// JIT engine — host scaffold.
//
// On the host we provide a minimal Engine that emits a textual
// disassembly of the IR (one line per instruction). The real backend
// (x86_64 / aarch64 / riscv64 codegen + executable page) lands with
// the kernel executable-format subsystem in Phase 1.

#include "neuro/jit/engine.hpp"

#include <sstream>
#include <string>

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

class HostEngine : public Engine {
public:
    [[nodiscard]] CodegenResult
        codegen(const Module& m, Target t) const override {
        CodegenResult r;
        r.target = t;
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
        r.disasm = d.str();
        return r;
    }

    [[nodiscard]] Value
        execute(const CodegenResult& /*code*/,
                std::span<const Value> /*args*/) const override {
        // No real execution on the host — return a zero.
        return Value{};
    }
};

}  // namespace

Engine& host_engine() {
    static HostEngine e;
    return e;
}

}  // namespace neuro::jit