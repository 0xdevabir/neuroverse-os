// src/bridge/ffi.cpp
//
// FFI — host scaffold. Provides the Bridge singleton factory.
// The real dynamic loader (dlopen / PE / Mach-O) lands in Phase 1.

#include "neuro/bridge/ffi.hpp"

namespace neuro::bridge {

Bridge& host_bridge() {
    static Bridge b;
    return b;
}

}  // namespace neuro::bridge