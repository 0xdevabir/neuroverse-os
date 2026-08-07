// neuro/pkg/store.hpp
//
// Content-addressed store (NeuroPkg, README §4.12).
//
// Per README §4.12 the package manager is content-addressed:
// every artifact (source tree, binary blob, capability bundle) is
// keyed by the SHA3-512 of its canonical bytes. On the host we
// back the Store with an in-memory map and use the real FIPS-202
// SHA3-512 hasher (see neuro/pkg/sha3.hpp) for content addressing.
// The on-disk Merkle tree + manifest verifier land with the
// kernel crypto subsystem in Phase 1.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neuro::pkg {

// SHA3-512 digest (64 bytes).
using Digest = std::array<std::uint8_t, 64>;

inline std::string to_hex(const Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(d.size() * 2, '0');
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[i * 2 + 0] = kHex[(d[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[d[i] & 0xF];
    }
    return out;
}

// One stored artifact: immutable bytes keyed by digest.
struct Artifact {
    Digest                          digest{};
    std::vector<std::byte>          bytes;
    std::filesystem::path           source_path;  // where it was imported from
};

// Manifest describing a package's contents: a list of named artifacts
// (each entry is a logical filename + its content digest).
struct ManifestEntry {
    std::string name;
    Digest      digest{};
};

struct Manifest {
    std::string                  package_name;
    std::string                  version;
    std::vector<ManifestEntry>   entries;
};

// Content-addressed store trait. Implementations may be in-memory
// or backed by a real CAS directory on disk.
class Store {
public:
    Store()                  = default;
    Store(const Store&)      = delete;
    Store& operator=(const Store&) = delete;
    virtual ~Store()         = default;

    // Hash arbitrary bytes with FIPS-202 SHA3-512.
    [[nodiscard]] virtual Digest
        hash(std::span<const std::byte> bytes) const noexcept = 0;

    // Ingest bytes; returns the digest under which they were stored.
    // No-op (returns existing digest) if the bytes are already present.
    virtual Digest put(std::span<const std::byte> bytes) = 0;

    // Ingest from a file path.
    virtual Digest put_file(const std::filesystem::path& p) = 0;

    // True if the digest is in the store.
    [[nodiscard]] virtual bool has(const Digest& d) const noexcept = 0;

    // Lookup; nullptr if missing.
    [[nodiscard]] virtual const Artifact*
        get(const Digest& d) const noexcept = 0;

    // Number of artifacts currently stored.
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

// Singleton factory: one Store per process.
Store& host_store();

}  // namespace neuro::pkg