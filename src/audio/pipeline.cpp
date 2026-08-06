// src/audio/pipeline.cpp
//
// Audio DSP pipeline — host scaffold. The graph + the built-in
// GainNode / PassthroughNode all live in the header; this TU
// exists so the subsystem is registered in the Makefile and
// linked into neuro_scratch alongside the other subsystems.

#include "neuro/audio/pipeline.hpp"

namespace neuro::audio {
// Translation unit anchor.
[[maybe_unused]] static int anchor = 0;
}  // namespace neuro::audio