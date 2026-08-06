// src/pkg/store.cpp
//
// Content-addressed store — host scaffold.
//
// On the host we back the Store with an unordered_map keyed by
// Digest. The hash() function uses std::hash on the byte stream as
// a stand-in for SHA3-512 (real algorithm lands with the kernel
// crypto subsystem in Phase 1).

#include "neuro/pkg/store.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace neuro::pkg {

namespace {

class HostStore : public Store {
public:
    [[nodiscard]] Digest
        hash(std::span<const std::byte> bytes) const noexcept override {
        Digest d{};
        // FNV-1a + length-mix stand-in for SHA3-512.
        std::uint64_t h = 1469598103934665603ULL;
        for (std::byte b : bytes) {
            h ^= static_cast<std::uint64_t>(b);
            h *= 1099511628211ULL;
        }
        // Splat the 64-bit hash across the 64-byte Digest so each
        // digest is non-trivially unique. Real SHA3-512 fills all
        // 64 bytes with cryptographic strength in Phase 1.
        for (std::size_t i = 0; i < d.size(); ++i) {
            d[i] = static_cast<std::uint8_t>((h >> ((i % 8) * 8)) ^ (i * 31));
        }
        return d;
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

}  // namespace neuro::pkg