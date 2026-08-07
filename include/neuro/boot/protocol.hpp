// neuro/boot/protocol.hpp
//
// Boot protocol (NeuroBoot, README §4.18).
//
// Per README §4.18 the boot process hands off a capability bundle
// from firmware to the kernel. The bundle carries:
//   - a manifest (kernel version, build id, hash of every segment),
//   - a list of segments (text / rodata / data / bss with load
//     addresses and sizes),
//   - a list of initial capabilities the kernel is given at start.
//   - a content_root + signature that prove the manifest was
//     produced by a holder of the boot trust root.
// On the host we expose the trait surface + a builder + parser
// using a stable text format; the real TLV-encoded binary bundle
// lands in Phase 1.
//
// Signing on the host:
//   signature = SHA3-512( trust_root || canonicalise(manifest) )
// This is a hash-MAC construction — adequate for the host
// scaffold but NOT a real signature scheme. The real Ed25519 /
// Dilithium signing lands with the kernel crypto subsystem in
// Phase 1; the host verifier is the same shape, so the wiring
// doesn't have to change when the algorithm does.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "neuro/core/capability.hpp"   // Capability
#include "neuro/pkg/digest.hpp"        // Digest256, Digest512

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
        // strip any leading whitespace
        std::size_t j = 0;
        while (j < val.size() && (val[j] == ' ' || val[j] == '\t')) ++j;
        val.erase(0, j);

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

// ---- Signed boot chain --------------------------------------------------
//
// The trust root is the SHA3-256 of the build-time signing secret.
// It's small enough to embed in firmware and serves as the symmetric
// key in the host-scaffold hash-MAC. Phase 1 swaps this for a real
// public-key signature; callers don't have to change.
//
//   sign(m, trust_root)   = SHA3-512( trust_root || canonicalise(m) )
//   verify(m, sig, trust_root) true iff sig == sign(m, trust_root)
//
// canonicalise() is a deterministic byte-string encoding of the
// manifest (see below).

using Signature = neuro::pkg::Digest512;  // SHA3-512 of (trust_root || bytes)
using TrustRoot = neuro::pkg::Digest256;  // SHA3-256 of the build-time secret

// Produces the exact byte-string that the signature covers. The
// format mirrors Manifest::canonicalise() in pkg/store.hpp so the
// host boot path and the host pkg path can share a verifier.
inline std::vector<std::byte>
canonicalise(const Manifest& m) {
    std::vector<std::byte> out;
    auto append_be32 = [&](std::uint32_t n) {
        out.push_back(static_cast<std::byte>((n >> 24) & 0xFF));
        out.push_back(static_cast<std::byte>((n >> 16) & 0xFF));
        out.push_back(static_cast<std::byte>((n >>  8) & 0xFF));
        out.push_back(static_cast<std::byte>( n        & 0xFF));
    };
    auto append_str = [&](const std::string& s) {
        append_be32(static_cast<std::uint32_t>(s.size()));
        out.insert(out.end(),
                   reinterpret_cast<const std::byte*>(s.data()),
                   reinterpret_cast<const std::byte*>(s.data()) + s.size());
    };
    auto append_be64 = [&](std::uint64_t n) {
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<std::byte>((n >> (i * 8)) & 0xFF));
        }
    };

    append_str(m.kernel_version);
    append_str(m.build_id);
    append_be32(static_cast<std::uint32_t>(m.segments.size()));
    for (const auto& s : m.segments) {
        append_str(s.name);
        // kind as 1 byte (Text=0, Rodata=1, Data=2, Bss=3)
        std::uint8_t kind_byte = 0;
        switch (s.kind) {
            case Segment::Kind::Text:   kind_byte = 0; break;
            case Segment::Kind::Rodata: kind_byte = 1; break;
            case Segment::Kind::Data:   kind_byte = 2; break;
            case Segment::Kind::Bss:    kind_byte = 3; break;
        }
        out.push_back(static_cast<std::byte>(kind_byte));
        append_be64(s.vaddr);
        append_be64(s.memsz);
        append_be64(s.filesz);
        append_be64(s.hash);
    }
    append_be32(static_cast<std::uint32_t>(m.capabilities.size()));
    for (const auto& c : m.capabilities) {
        // The capability carries its own opaque 16-byte body; emit it
        // raw for verification. The to_string() form is human-readable
        // and isn't suitable for hashing.
        const std::uint8_t* cap_bytes = reinterpret_cast<const std::uint8_t*>(&c);
        out.insert(out.end(),
                   reinterpret_cast<const std::byte*>(cap_bytes),
                   reinterpret_cast<const std::byte*>(cap_bytes + sizeof(c)));
    }
    return out;
}

// Compute the SHA3-512 of (trust_root || canonicalise(m)). The
// trust_root is used as a domain-separation prefix so signatures
// for different roots never collide. The hash covers every byte
// of the canonical encoding.
Signature sign(const Manifest& m, const TrustRoot& trust_root);

// True iff `sig` equals SHA3-512(trust_root || canonicalise(m)).
[[nodiscard]] bool verify(const Manifest& m,
                          const Signature& sig,
                          const TrustRoot& trust_root) noexcept;

// Best-effort convenience: derive a trust_root from a human-readable
// passphrase. Real firmware bakes this in at build time; the host
// scaffold accepts any UTF-8 string.
[[nodiscard]] TrustRoot trust_root_from_passphrase(
    std::string_view passphrase) noexcept;

// Loader for a "known-good" boot manifest. Calls verify() with the
// host's hard-coded trust_root; returns std::nullopt if the manifest
// doesn't authenticate.
[[nodiscard]] std::optional<Manifest>
load_signed_manifest(const Manifest& m, const Signature& sig) noexcept;

}  // namespace neuro::boot