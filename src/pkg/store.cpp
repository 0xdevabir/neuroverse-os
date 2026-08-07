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

}  // namespace neuro::pkg