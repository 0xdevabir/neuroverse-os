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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "neuro/pkg/digest.hpp"

namespace neuro::pkg {

// SHA3-512 digest (64 bytes).
using Digest = Digest512;

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

class Store;  // defined later in this header

struct Manifest {
    std::string                  package_name;
    std::string                  version;
    std::vector<ManifestEntry>   entries;

    // Optional content root recorded by the signer at build time.
    // When present, Manifest::verify() compares it against the
    // locally recomputed root to detect tampering.
    std::optional<Digest>        trusted_root;

    // Canonicalise the manifest into a flat byte string. The format
    // is a simple length-prefixed encoding so adding a new field
    // can't accidentally match an old digest:
    //
    //   [4 bytes BE: package_name.length]
    //   [N bytes:    package_name]
    //   [4 bytes BE: version.length]
    //   [M bytes:    version]
    //   [4 bytes BE: entries.size()]
    //   for each entry:
    //     [4 bytes BE: name.length]
    //     [K bytes:    name]
    //     [64 bytes:   digest]
    [[nodiscard]] std::vector<std::byte> canonicalise() const;

    // Compute the SHA3-512 of canonicalise(). Two manifests with the
    // same content root are byte-equivalent in our encoding; this is
    // the value the manifest is signed against (or stored under).
    [[nodiscard]] Digest content_root() const noexcept;

    // Verify the manifest against a Store. A manifest is valid iff
    //   1. every entry's digest is present in the store, and
    //   2. if a trusted_root was recorded, the locally recomputed
    //      content_root matches it.
    //
    // Returns std::nullopt on success. On failure, returns a short
    // human-readable reason. Verifying against a Store ensures a
    // malicious manifest can't claim to contain a blob that has
    // never been ingested.
    [[nodiscard]] std::optional<std::string>
    verify(const Store& s) const noexcept;
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