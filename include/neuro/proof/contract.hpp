// neuro/proof/contract.hpp
//
// Contract attributes skeleton (NeuroProof, README §4.14).
//
// Per README §4.14 every kernel and library function may carry
// [[expects]] and [[ensures]] annotations that the prover (an SMT
// solver) consumes to verify safety properties. In addition,
// callers may attach [[invariant]] blocks to loops and [[measure]]
// expressions to termination functions.
//
// On the host we expose the macros + a thin Contract<T> helper that
// runtime-checks the precondition and stores the postcondition for
// later inspection. The real SMT-backed verification lands with the
// NeuroProof toolchain in Phase 1.

#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace neuro::proof {

// A labelled contract that the prover can attach to a function.
struct Contract {
    std::string expects;   // pre-condition expression
    std::string ensures;   // post-condition expression
    std::string invariant; // loop / class invariant (optional)
    std::string measure;   // termination measure (optional)

    // Pretty-print the contract in a stable format.
    [[nodiscard]] std::string to_string() const {
        std::ostringstream s;
        if (!expects.empty())   s << "[[expects: "   << expects   << "]]\n";
        if (!ensures.empty())   s << "[[ensures: "   << ensures   << "]]\n";
        if (!invariant.empty()) s << "[[invariant: " << invariant << "]]\n";
        if (!measure.empty())   s << "[[measure: "   << measure   << "]]\n";
        return s.str();
    }
};

// Helper: wrap a callable with a runtime pre-condition check. The
// `check` callable returns true when the precondition holds.
template <typename F, typename Check>
auto with_precondition(F&& f, Check&& check, std::string_view expr) {
    return [f = std::forward<F>(f),
            check = std::forward<Check>(check),
            expr{std::string(expr)}](auto&&... args) -> decltype(auto) {
        if (!check(std::forward<decltype(args)>(args)...)) {
            std::fprintf(stderr,
                         "neuro::proof: precondition failed: %s\n",
                         expr.c_str());
        }
        return f(std::forward<decltype(args)>(args)...);
    };
}

}  // namespace neuro::proof

// Source-level annotations. These are no-ops on the host — the
// prover parses the source directly. They exist so contract-bearing
// code compiles cleanly and the annotations are visible to readers.

// [[expects: <expr>]]  — pre-condition
// [[ensures: <expr>]]  — post-condition
// [[invariant: <expr>]] — loop or class invariant
// [[measure: <expr>]]   — termination measure
//
// These are written as ordinary C++ attributes that are silently
// ignored by the compiler. Vendors may add real semantics via plugins
// in Phase 1.
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(expects)
#    define NEURO_EXPECTS(...) [[expects: __VA_ARGS__]]
#  else
#    define NEURO_EXPECTS(...)
#  endif
#  if __has_cpp_attribute(ensures)
#    define NEURO_ENSURES(...) [[ensures: __VA_ARGS__]]
#  else
#    define NEURO_ENSURES(...)
#  endif
#  if __has_cpp_attribute(invariant)
#    define NEURO_INVARIANT(...) [[invariant: __VA_ARGS__]]
#  else
#    define NEURO_INVARIANT(...)
#  endif
#  if __has_cpp_attribute(measure)
#    define NEURO_MEASURE(...) [[measure: __VA_ARGS__]]
#  else
#    define NEURO_MEASURE(...)
#  endif
#else
#  define NEURO_EXPECTS(...)
#  define NEURO_ENSURES(...)
#  define NEURO_INVARIANT(...)
#  define NEURO_MEASURE(...)
#endif
