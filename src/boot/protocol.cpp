// src/boot/protocol.cpp
//
// Boot protocol — host scaffold. Provides the host_boot_manifest()
// factory that returns a single minimal manifest describing the
// host scaffold, plus a SHA3-512-based signing/verification path
// modelled after Phase-1's Ed25519/Dilithium swap. The real TLV
// parser + firmware handoff + real signature scheme land in
// Phase 1.

#include "neuro/boot/protocol.hpp"
#include "neuro/pkg/sha3.hpp"

#include <span>

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

// ---- Signed-boot chain --------------------------------------------------

namespace {

// The host scaffold hard-codes a single signing secret and derives
// its trust_root from SHA3-256 of that secret. Real firmware bakes
// in the trust_root at build time and never sees the secret.
constexpr const char* kHostSigningSecret =
    "neuroverse-os-host-boot-secret";

TrustRoot host_trust_root() {
    TrustRoot r = neuro::pkg::sha3::sha3_256(
        std::string_view{kHostSigningSecret});
    return r;
}

}  // namespace

Signature sign(const Manifest& m, const TrustRoot& trust_root) {
    // Build the preimage: trust_root || canonicalise(m).
    auto bytes = canonicalise(m);
    std::vector<std::byte> preimage;
    preimage.reserve(bytes.size() + trust_root.size());
    preimage.insert(preimage.end(),
                     reinterpret_cast<const std::byte*>(trust_root.data()),
                     reinterpret_cast<const std::byte*>(trust_root.data() +
                                                        trust_root.size()));
    preimage.insert(preimage.end(), bytes.begin(), bytes.end());
    return neuro::pkg::sha3::sha3_512(preimage);
}

bool verify(const Manifest& m,
            const Signature& sig,
            const TrustRoot& trust_root) noexcept {
    Signature expected = sign(m, trust_root);
    return expected == sig;
}

TrustRoot trust_root_from_passphrase(std::string_view passphrase) noexcept {
    return neuro::pkg::sha3::sha3_256(passphrase);
}

std::optional<Manifest>
load_signed_manifest(const Manifest& m, const Signature& sig) noexcept {
    TrustRoot root = host_trust_root();
    if (verify(m, sig, root)) return m;
    return std::nullopt;
}

}  // namespace neuro::boot