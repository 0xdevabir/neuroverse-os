// neuro/boot/protocol.hpp
//
// Boot protocol skeleton (NeuroBoot, README §4.18).
//
// Per README §4.18 the boot process hands off a capability bundle
// from firmware to the kernel. The bundle carries:
//   - a manifest (kernel version, build id, hash of every segment),
//   - a list of segments (text / rodata / data / bss with load
//     addresses and sizes),
//   - a list of initial capabilities the kernel is given at start.
// On the host we expose the trait surface + a builder + parser
// using a stable text format; the real TLV-encoded binary bundle
// lands in Phase 1.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "neuro/core/capability.hpp"   // Capability

namespace neuro::boot {

// One loadable segment. Real segments map to ELF/program headers
// in Phase 1.
struct Segment {
    enum class Kind : std::uint8_t { Text, Rodata, Data, Bss };
    Kind            kind = Kind::Text;
    std::string     name;       // ".text", ".rodata", ...
    std::uint64_t   vaddr  = 0; // virtual load address
    std::uint64_t   memsz  = 0; // in-memory size
    std::uint64_t   filesz = 0; // bytes present in the bundle
    std::uint64_t   hash   = 0; // 64-bit content hash (real SHA3-512 in Phase 1)
};

// Top-level boot manifest.
struct Manifest {
    std::string                 kernel_version;
    std::string                 build_id;
    std::vector<Segment>        segments;
    std::vector<neuro::core::Capability> capabilities;
};

// One line of the stable text representation.
inline std::string to_text(const Manifest& m) {
    std::string out;
    out += "version: " + m.kernel_version + "\n";
    out += "build:   " + m.build_id + "\n";
    for (auto& s : m.segments) {
        const char* k = "?";
        switch (s.kind) {
            case Segment::Kind::Text:   k = "text";   break;
            case Segment::Kind::Rodata: k = "rodata"; break;
            case Segment::Kind::Data:   k = "data";   break;
            case Segment::Kind::Bss:    k = "bss";    break;
        }
        out += "segment: ";
        out += std::string(k) + " " + s.name + " "
             + std::to_string(s.vaddr) + " "
             + std::to_string(s.memsz) + " "
             + std::to_string(s.filesz) + " "
             + std::to_string(s.hash) + "\n";
    }
    for (auto& c : m.capabilities) {
        out += "cap: " + std::string(c.to_string()) + "\n";
    }
    return out;
}

// Best-effort parser. Real parser is TLV-based in Phase 1.
// Returns true on success and fills `out`. The host parser is
// best-effort: it accepts any well-formed version/build line.
inline bool parse_text(const std::string& s, Manifest& out) {
    out = Manifest{};
    std::size_t i = 0;
    while (i < s.size()) {
        auto eol = s.find('\n', i);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(i, eol - i);
        i = eol + 1;
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // strip one optional leading space
        if (!val.empty() && val.front() == ' ') val.erase(0, 1);

        if (key == "version")      out.kernel_version = val;
        else if (key == "build")   out.build_id       = val;
        else if (key == "segment") {
            // kind name vaddr memsz filesz hash
            Segment seg;
            std::size_t p = 0;
            auto next = [&](std::string& out2) {
                auto sp = val.find(' ', p);
                out2 = val.substr(p, sp - p);
                p = sp == std::string::npos ? val.size() : sp + 1;
            };
            std::string kind, name, vaddr, memsz, filesz, hash;
            next(kind); next(name); next(vaddr); next(memsz); next(filesz); next(hash);
            if      (kind == "text")   seg.kind = Segment::Kind::Text;
            else if (kind == "rodata") seg.kind = Segment::Kind::Rodata;
            else if (kind == "data")   seg.kind = Segment::Kind::Data;
            else if (kind == "bss")    seg.kind = Segment::Kind::Bss;
            seg.name   = name;
            seg.vaddr  = std::stoull(vaddr);
            seg.memsz  = std::stoull(memsz);
            seg.filesz = std::stoull(filesz);
            seg.hash   = std::stoull(hash);
            out.segments.push_back(std::move(seg));
        }
        else if (key == "cap") {
            // The real TLV parser round-trips caps; the host stub
            // just leaves the capability list empty (caps are not
            // reified yet on the host scaffold).
        }
    }
    return !out.kernel_version.empty();
}

// Singleton factory: parses from a known text or returns a default
// empty manifest. Real implementation reads from firmware in Phase 1.
Manifest host_boot_manifest();

}  // namespace neuro::boot