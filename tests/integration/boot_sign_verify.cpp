// tests/integration/boot_sign_verify.cpp
//
// Z7.8 — boot manifest signed then verified end-to-end.
//
// Demonstrates the full NeuroBoot chain:
//   1. Build a Manifest with kernel version, build id, segments,
//      and initial capabilities.
//   2. Sign it with a trust root derived from a passphrase.
//   3. Verify the signature succeeds.
//   4. Tamper with the manifest → verify fails.
//   5. Sign with a different trust root → verify fails.
//   6. Round-trip via to_text / parse_text and re-verify.

#include "tests/test_framework.hpp"

#include "neuro/boot/protocol.hpp"
#include "neuro/core/capability.hpp"
#include "neuro/pkg/digest.hpp"

#include <cstdint>
#include <cstring>
#include <string>

using neuro::boot::canonicalise;
using neuro::boot::Manifest;
using neuro::boot::Segment;
using neuro::boot::sign;
using neuro::boot::Signature;
using neuro::boot::TrustRoot;
using neuro::boot::trust_root_from_passphrase;
using neuro::boot::verify;
using neuro::core::Capability;
using neuro::core::CapRight;

namespace {

Manifest build_demo_manifest() {
    Manifest m;
    m.kernel_version = "neuro-0.1.0";
    m.build_id       = "build-2026-08-07";

    Segment text;
    text.kind    = Segment::Kind::Text;
    text.name    = ".text";
    text.vaddr   = 0xFFFF'8000'0000'0000ULL;
    text.memsz   = 0x100000;
    text.filesz  = 0x100000;
    text.hash    = 0xDEADBEEFCAFEBABEULL;
    m.segments.push_back(text);

    Segment rodata;
    rodata.kind   = Segment::Kind::Rodata;
    rodata.name   = ".rodata";
    rodata.vaddr  = 0xFFFF'8000'0010'0000ULL;
    rodata.memsz  = 0x10000;
    rodata.filesz = 0x10000;
    rodata.hash   = 0x1234567890ABCDEFULL;
    m.segments.push_back(rodata);

    // Initial cap for the kernel.
    Capability init_cap = Capability::mint(
        /*object_id=*/0xC0DE0001, CapRight::Read | CapRight::Write,
        /*epoch=*/0, /*gen=*/1);
    m.capabilities.push_back(init_cap);

    return m;
}

}  // namespace

TEST(boot_sign_verify, sign_then_verify_succeeds) {
    TrustRoot root = trust_root_from_passphrase("test-passphrase");
    Manifest m = build_demo_manifest();

    Signature sig = sign(m, root);
    EXPECT_TRUE(verify(m, sig, root));

    // The signature should be deterministic.
    Signature sig2 = sign(m, root);
    EXPECT_TRUE(sig == sig2);
}

TEST(boot_sign_verify, tampered_manifest_fails_verification) {
    TrustRoot root = trust_root_from_passphrase("test-passphrase");
    Manifest m = build_demo_manifest();
    Signature sig = sign(m, root);

    // Tamper with the kernel version.
    m.kernel_version = "neuro-0.1.0-EVIL";
    EXPECT_FALSE(verify(m, sig, root));

    // Restore and tamper with a segment hash.
    m = build_demo_manifest();
    m.segments[0].hash ^= 0x1;
    EXPECT_FALSE(verify(m, sig, root));
}

TEST(boot_sign_verify, wrong_trust_root_fails_verification) {
    TrustRoot root_a = trust_root_from_passphrase("root-A");
    TrustRoot root_b = trust_root_from_passphrase("root-B");
    Manifest m = build_demo_manifest();

    Signature sig = sign(m, root_a);
    EXPECT_TRUE(verify(m, sig, root_a));
    EXPECT_FALSE(verify(m, sig, root_b));
}

TEST(boot_sign_verify, roundtrip_via_text_format) {
    TrustRoot root = trust_root_from_passphrase("roundtrip");

    // Use a manifest without capabilities — the host text parser
    // does not round-trip caps (that's the TLV format's job in
    // Phase 1). The version/build/segments paths are still covered.
    Manifest m;
    m.kernel_version = "neuro-0.1.0";
    m.build_id       = "build-rt";

    Segment text;
    text.kind    = Segment::Kind::Text;
    text.name    = ".text";
    text.vaddr   = 0x1000;
    text.memsz   = 0x100;
    text.filesz  = 0x100;
    text.hash    = 0xAA;
    m.segments.push_back(text);

    Signature sig = sign(m, root);

    auto text_str = neuro::boot::to_text(m);
    Manifest parsed;
    EXPECT_TRUE(neuro::boot::parse_text(text_str, parsed));
    EXPECT_EQ(m.kernel_version, parsed.kernel_version);
    EXPECT_EQ(m.build_id, parsed.build_id);
    EXPECT_EQ(m.segments.size(), parsed.segments.size());
    EXPECT_EQ(m.segments[0].vaddr, parsed.segments[0].vaddr);
    EXPECT_EQ(m.segments[0].hash,  parsed.segments[0].hash);

    // Signature verifies on the parsed copy.
    EXPECT_TRUE(verify(parsed, sig, root));
}

TEST(boot_sign_verify, canonicalise_is_deterministic) {
    Manifest m = build_demo_manifest();
    auto a = canonicalise(m);
    auto b = canonicalise(m);
    EXPECT_EQ(a.size(), b.size());
    EXPECT_TRUE(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

TEST(boot_sign_verify, signature_changes_when_capabilities_change) {
    TrustRoot root = trust_root_from_passphrase("caps");
    Manifest m = build_demo_manifest();
    Signature sig_orig = sign(m, root);

    m.capabilities.push_back(Capability::mint(
        0xC0DE0002, CapRight::Read, 0, 1));

    Signature sig_after = sign(m, root);
    EXPECT_TRUE(sig_orig != sig_after);
}

RUN_ALL_TESTS()