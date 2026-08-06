// src/boot/protocol.cpp
//
// Boot protocol — host scaffold. Provides the host_boot_manifest()
// factory that returns a single minimal manifest describing the
// host scaffold. The real TLV parser + firmware handoff lands in
// Phase 1.

#include "neuro/boot/protocol.hpp"

namespace neuro::boot {

Manifest host_boot_manifest() {
    Manifest m;
    m.kernel_version = "0.1.0-host";
    m.build_id       = "host-scaffold";
    Segment text;
    text.kind   = Segment::Kind::Text;
    text.name   = ".text";
    text.vaddr  = 0x1000;
    text.memsz  = 0x10000;
    text.filesz = 0x10000;
    text.hash   = 0;
    m.segments.push_back(text);
    return m;
}

}  // namespace neuro::boot