// tests/unit/boot/protocol_test.cpp
//
// Tests for the boot-time signing/verification chain (NeuroBoot
// §4.18). The host scaffold uses a hash-MAC over (trust_root ||
// canonicalise(manifest)); Phase 1 swaps in a real Ed25519 /
// Dilithium signature. The verifier shape doesn't change.
//
//   1. sign + verify with the same trust_root succeeds.
//   2. sign + verify with a different trust_root fails.
//   3. Tampered manifest (after sign) fails verification.
//   4. canonicalise() is order-sensitive for segments and caps.
//   5. trust_root_from_passphrase is deterministic.
//   6. load_signed_manifest() round-trips with the host's hard-coded
//      trust root.
//   7. parse_text + sign + verify round-trips through the text form.

#include "neuro/boot/protocol.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::boot::canonicalise;
using neuro::boot::host_boot_manifest;
using neuro::boot::load_signed_manifest;
using neuro::boot::Manifest;
using neuro::boot::Segment;
using neuro::boot::sign;
using neuro::boot::Signature;
using neuro::boot::trust_root_from_passphrase;
using neuro::boot::TrustRoot;
using neuro::boot::verify;

using neuro::core::Capability;
using neuro::core::CapRight;

// ---- 1. sign + verify with the same trust_root ------------------------

TEST(boot_sign, round_trip_passes) {
    Manifest m = host_boot_manifest();
    TrustRoot root = trust_root_from_passphrase("test-passphrase-1");
    Signature sig = sign(m, root);
    EXPECT_TRUE(verify(m, sig, root));
}

// ---- 2. sign + verify with a different trust_root fails ---------------

TEST(boot_sign, wrong_trust_root_fails) {
    Manifest m = host_boot_manifest();
    TrustRoot root_a = trust_root_from_passphrase("alice");
    TrustRoot root_b = trust_root_from_passphrase("bob");
    Signature sig = sign(m, root_a);
    EXPECT_FALSE(verify(m, sig, root_b));
    EXPECT_TRUE(verify(m, sig, root_a));
}

// ---- 3. Tampered manifest fails verification -------------------------

TEST(boot_sign, tampered_manifest_fails) {
    Manifest m = host_boot_manifest();
    TrustRoot root = trust_root_from_passphrase("alice");
    Signature sig = sign(m, root);

    // Mutate after signing: bump the build_id.
    Manifest evil = m;
    evil.build_id = "host-scaffold-EVIL";
    EXPECT_FALSE(verify(evil, sig, root));
}

TEST(boot_sign, segment_removed_fails) {
    Manifest m;
    m.kernel_version = "0.1.0";
    m.build_id       = "b1";
    Segment s1; s1.name = ".text";   s1.vaddr = 0x1000;
    Segment s2; s2.name = ".rodata"; s2.vaddr = 0x2000;
    m.segments.push_back(s1);
    m.segments.push_back(s2);

    TrustRoot root = trust_root_from_passphrase("k");
    Signature sig = sign(m, root);

    Manifest stripped = m;
    stripped.segments.pop_back();
    EXPECT_FALSE(verify(stripped, sig, root));
}

// ---- 4. canonicalise() is order-sensitive ----------------------------

TEST(boot_sign, canonicalise_segment_order_matters) {
    Manifest m1;
    Segment s1; s1.name = "a"; s1.vaddr = 0x1000;
    Segment s2; s2.name = "b"; s2.vaddr = 0x2000;
    m1.segments.push_back(s1);
    m1.segments.push_back(s2);

    Manifest m2;
    m2.segments.push_back(s2);
    m2.segments.push_back(s1);

    auto c1 = canonicalise(m1);
    auto c2 = canonicalise(m2);
    EXPECT_TRUE(c1 != c2);
}

TEST(boot_sign, canonicalise_cap_order_matters) {
    Capability c1 = Capability::mint(/*oid*/ 1, CapRight::Read,
                                     /*ep*/ 1, /*gen*/ 1);
    Capability c2 = Capability::mint(/*oid*/ 2, CapRight::Write,
                                     /*ep*/ 1, /*gen*/ 1);

    Manifest m1;
    m1.capabilities = {c1, c2};
    Manifest m2;
    m2.capabilities = {c2, c1};

    auto bytes1 = canonicalise(m1);
    auto bytes2 = canonicalise(m2);
    EXPECT_TRUE(bytes1 != bytes2);
}

// ---- 5. trust_root_from_passphrase is deterministic -----------------

TEST(boot_sign, trust_root_is_deterministic) {
    auto r1 = trust_root_from_passphrase("hello");
    auto r2 = trust_root_from_passphrase("hello");
    auto r3 = trust_root_from_passphrase("world");
    EXPECT_EQ(r1, r2);
    EXPECT_TRUE(r1 != r3);
}

// ---- 6. load_signed_manifest with the host's hard-coded root --------

TEST(boot_sign, load_signed_manifest_round_trip) {
    Manifest m = host_boot_manifest();
    // Use the same passphrase the host boot path uses.
    TrustRoot root = trust_root_from_passphrase(
        "neuroverse-os-host-boot-secret");
    Signature sig = sign(m, root);

    auto loaded = load_signed_manifest(m, sig);
    EXPECT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->kernel_version, m.kernel_version);
    EXPECT_EQ(loaded->build_id, m.build_id);
}

TEST(boot_sign, load_signed_manifest_rejects_bad_signature) {
    Manifest m = host_boot_manifest();
    TrustRoot root = trust_root_from_passphrase(
        "neuroverse-os-host-boot-secret");
    Signature sig = sign(m, root);

    // Mutate manifest, keep the signature.
    Manifest evil = m;
    evil.kernel_version = "0.99.0-evil";
    auto loaded = load_signed_manifest(evil, sig);
    EXPECT_FALSE(loaded.has_value());
}

// ---- 7. parse_text + sign + verify round-trips ----------------------

TEST(boot_sign, parse_text_sign_verify_round_trip) {
    using neuro::boot::parse_text;
    using neuro::boot::to_text;

    Manifest m = host_boot_manifest();
    std::string text = to_text(m);

    Manifest parsed;
    EXPECT_TRUE(parse_text(text, parsed));
    EXPECT_EQ(parsed.kernel_version, m.kernel_version);
    EXPECT_EQ(parsed.build_id, m.build_id);

    TrustRoot root = trust_root_from_passphrase("test-passphrase-2");
    Signature sig = sign(parsed, root);
    EXPECT_TRUE(verify(parsed, sig, root));
}

RUN_ALL_TESTS()