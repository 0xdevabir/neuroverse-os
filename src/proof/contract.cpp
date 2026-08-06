// src/proof/contract.cpp
//
// Contract attributes — host scaffold.
//
// The macros + helpers are header-only; this TU exists so the
// subsystem is registered in the Makefile and linked into
// neuro_scratch alongside the other subsystems.

#include "neuro/proof/contract.hpp"

namespace neuro::proof {
// Translation unit anchor.
[[maybe_unused]] static int anchor = 0;
}  // namespace neuro::proof