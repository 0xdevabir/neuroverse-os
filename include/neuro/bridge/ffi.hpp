// neuro/bridge/ffi.hpp
//
// FFI skeleton (NeuroBridge, README §4.17).
//
// Per README §4.17 NeuroBridge lets native libraries (and other
// languages) interoperate with NeuroVerse programs through a
// capability-gated C-ABI. A symbol is identified by (name, version)
// and resolved against a Library handle. On the host we expose the
// trait surface + a small in-process symbol table; the real dynamic
// loader (dlopen / PE / Mach-O) lands in Phase 1.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace neuro::bridge {

// Opaque native function pointer. Use `reinterpret_cast` to call it.
// On the host we just store a `void*`; the real loader provides
// platform-specific function pointers in Phase 1.
using NativeFn = void (*)();

// One symbol: a named, versioned native function.
struct Symbol {
    std::string          name;
    std::string          version;
    NativeFn             fn = nullptr;
};

// One library: a name + an ordered list of symbols. Real libraries
// map to dlopen'd handles in Phase 1; on the host we keep them in
// a registry keyed by name.
class Library {
public:
    explicit Library(std::string name) : name_(std::move(name)) {}
    Library(const Library&)               = delete;
    Library& operator=(const Library&)    = delete;

    void add(Symbol s) { symbols_.push_back(std::move(s)); }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    [[nodiscard]] const Symbol*
        find(const std::string& sym, const std::string& version) const {
        for (auto& s : symbols_) {
            if (s.name == sym && s.version == version) return &s;
        }
        return nullptr;
    }

private:
    std::string             name_;
    std::vector<Symbol>     symbols_;
};

// Bridge: the process-wide collection of registered libraries.
class Bridge {
public:
    Bridge()                                  = default;
    Bridge(const Bridge&)                     = delete;
    Bridge& operator=(const Bridge&)          = delete;

    // Register or replace a library.
    void register_library(std::unique_ptr<Library> lib) {
        std::string n = lib->name();
        libs_[n] = std::move(lib);
    }

    // Look up a symbol across all registered libraries.
    [[nodiscard]] NativeFn
        resolve(const std::string& sym, const std::string& version) const {
        for (auto& kv : libs_) {
            if (auto* s = kv.second->find(sym, version)) return s->fn;
        }
        return nullptr;
    }

    // Count of registered libraries.
    [[nodiscard]] std::size_t library_count() const noexcept {
        return libs_.size();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Library>>  libs_;
};

// Thrown when a symbol cannot be resolved.
struct ResolveError : std::runtime_error {
    explicit ResolveError(const std::string& w)
        : std::runtime_error("bridge: cannot resolve symbol: " + w) {}
};

// Singleton factory: one Bridge per process.
Bridge& host_bridge();

}  // namespace neuro::bridge