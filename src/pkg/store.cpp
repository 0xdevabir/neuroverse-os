// src/pkg/store.cpp
//
// Content-addressed store — host scaffold.
//
// On the host we back the Store with an std::map keyed by Digest
// and use the real FIPS-202 SHA3-512 hasher (neuro/pkg/sha3.hpp)
// for content addressing. The on-disk Merkle tree + manifest
// verifier land with the kernel crypto subsystem in Phase 1.

#include "neuro/pkg/store.hpp"
#include "neuro/pkg/sha3.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace neuro::pkg {

namespace {

class HostStore : public Store {
public:
    [[nodiscard]] Digest
        hash(std::span<const std::byte> bytes) const noexcept override {
        return sha3::sha3_512(bytes);
    }

    Digest put(std::span<const std::byte> bytes) override {
        const Digest d = hash(bytes);
        std::lock_guard<std::mutex> g(mu_);
        if (artifacts_.find(d) == artifacts_.end()) {
            Artifact a;
            a.digest = d;
            a.bytes.assign(bytes.begin(), bytes.end());
            artifacts_.emplace(d, std::move(a));
        }
        return d;
    }

    Digest put_file(const std::filesystem::path& p) override {
        std::ifstream f(p, std::ios::binary);
        if (!f) return Digest{};
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string s = ss.str();
        std::vector<std::byte> v(s.size());
        std::memcpy(v.data(), s.data(), s.size());
        Digest d = put(v);
        std::lock_guard<std::mutex> g(mu_);
        auto it = artifacts_.find(d);
        if (it != artifacts_.end()) it->second.source_path = p;
        return d;
    }

    [[nodiscard]] bool has(const Digest& d) const noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        return artifacts_.find(d) != artifacts_.end();
    }

    [[nodiscard]] const Artifact*
        get(const Digest& d) const noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        auto it = artifacts_.find(d);
        return it == artifacts_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        return artifacts_.size();
    }

private:
    mutable std::mutex              mu_;
    std::map<Digest, Artifact>      artifacts_;
};

}  // namespace

Store& host_store() {
    static HostStore s;
    return s;
}

// ---- Manifest implementation -------------------------------------------
//
// The canonical encoding is deliberately explicit so the SHA3-512 of
// it is independent of platform word size / struct layout. Each
// segment is prefixed with its length in big-endian to disambiguate
// string boundaries.

namespace {

// Append `n` (big-endian, 4 bytes) followed by `data` (length
// bytes) into `out`. Used for length-prefixed fields.
inline void append_be32(std::vector<std::byte>& out,
                        std::uint32_t n) {
    out.push_back(static_cast<std::byte>((n >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((n >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((n >>  8) & 0xFF));
    out.push_back(static_cast<std::byte>( n        & 0xFF));
}

inline void append_bytes(std::vector<std::byte>& out,
                         std::span<const std::byte> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void append_string(std::vector<std::byte>& out,
                          std::string_view s) {
    append_be32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(s.data()),
               reinterpret_cast<const std::byte*>(s.data()) + s.size());
}

}  // namespace

std::vector<std::byte> Manifest::canonicalise() const {
    std::vector<std::byte> out;
    append_string(out, package_name);
    append_string(out, version);
    append_be32(out, static_cast<std::uint32_t>(entries.size()));
    for (const auto& e : entries) {
        append_string(out, e.name);
        append_bytes(out, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(e.digest.data()),
            e.digest.size()));
    }
    return out;
}

Digest Manifest::content_root() const noexcept {
    auto bytes = canonicalise();
    return sha3::sha3_512(bytes);
}

std::optional<std::string>
Manifest::verify(const Store& s) const noexcept {
    // (1) every entry's digest must be present in the store.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!s.has(entries[i].digest)) {
            return "manifest entry \"" + entries[i].name +
                   "\" (index " + std::to_string(i) +
                   ") not present in store";
        }
    }

    // (2) if the signer recorded a trusted_root at build time, the
    //     locally recomputed content_root must match it. This is
    //     what catches tamper: an attacker who can edit entries
    //     will recompute their way to a new root that doesn't
    //     match the trusted value.
    Digest local = content_root();
    if (trusted_root.has_value() && local != *trusted_root) {
        return "content_root mismatch — manifest does not match "
               "trusted_root";
    }

    return std::nullopt;
}

}  // namespace neuro::pkg