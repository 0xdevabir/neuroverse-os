// tests/unit/pkg/manifest_test.cpp
//
// Tests for Manifest::canonicalise() / Manifest::content_root() /
// Manifest::verify(). Covers:
//   - canonical encoding is deterministic and order-sensitive
//   - content_root changes when any field changes
//   - verify() succeeds when every entry is present in the store
//   - verify() fails when an entry is missing
//   - verify() catches tampering via trusted_root mismatch
//   - the empty-manifest case is well-defined

#include "neuro/pkg/store.hpp"
#include "neuro/pkg/sha3.hpp"

#include <cstring>
#include <span>
#include <string>

#include "../../test_framework.hpp"

using neuro::pkg::Digest;
using neuro::pkg::Digest512;
using neuro::pkg::Manifest;
using neuro::pkg::ManifestEntry;
using neuro::pkg::Store;
using neuro::pkg::sha3::sha3_512;

static std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return b;
}

// ---- 1. Canonical encoding is deterministic ----------------------------
//
// Absorbing the same bytes twice yields the same content_root.
// Adding a single byte to package_name changes the root.

TEST(manifest, content_root_is_deterministic) {
    Manifest m1;
    m1.package_name = "neuroverse-os";
    m1.version      = "0.1.0";
    m1.entries      = {
        {"/bin/hello", sha3_512(std::string_view{"hello"})},
        {"/bin/world", sha3_512(std::string_view{"world"})},
    };

    Digest r1 = m1.content_root();
    Digest r2 = m1.content_root();
    EXPECT_EQ(r1, r2);
}

TEST(manifest, content_root_changes_with_package_name) {
    Manifest m1; m1.package_name = "alpha";
    Manifest m2; m2.package_name = "beta";
    EXPECT_TRUE(m1.content_root() != m2.content_root());
}

TEST(manifest, content_root_changes_with_version) {
    Manifest m1; m1.version = "0.1.0";
    Manifest m2; m2.version = "0.2.0";
    EXPECT_TRUE(m1.content_root() != m2.content_root());
}

TEST(manifest, content_root_changes_with_entry_order) {
    auto h_lo = sha3_512(std::string_view{"lo"});
    auto h_hi = sha3_512(std::string_view{"hi"});
    Manifest m1;
    m1.entries = { {"x", h_lo}, {"y", h_hi} };
    Manifest m2;
    m2.entries = { {"y", h_hi}, {"x", h_lo} };
    EXPECT_TRUE(m1.content_root() != m2.content_root());
}

TEST(manifest, content_root_changes_with_entry_digest) {
    Manifest m1;
    m1.entries = { {"a", sha3_512(std::string_view{"abc"})} };
    Manifest m2;
    m2.entries = { {"a", sha3_512(std::string_view{"abd"})} };
    EXPECT_TRUE(m1.content_root() != m2.content_root());
}

// ---- 2. Round-trip: ingest bytes → build manifest → verify ------------

TEST(manifest, end_to_end_round_trip_passes_verify) {
    auto& s = neuro::pkg::host_store();

    // Ingest three blobs into the store.
    Digest d_logo   = s.put(bytes_of("NEUROVERSE_LOGO_BYTES"));
    Digest d_kern   = s.put(bytes_of("KERNEL_IMAGE_PAYLOAD"));
    Digest d_config = s.put(bytes_of("config: debug=true"));

    // Build a manifest pointing at them.
    Manifest m;
    m.package_name = "neuroverse-os";
    m.version      = "0.1.0";
    m.entries      = {
        {"/boot/logo.bin",   d_logo},
        {"/boot/kernel.bin", d_kern},
        {"/etc/init.cfg",    d_config},
    };
    m.trusted_root = m.content_root();

    EXPECT_FALSE(m.verify(s).has_value());
}

// ---- 3. Missing entry causes verify() failure -------------------------

TEST(manifest, verify_fails_when_entry_missing) {
    auto& s = neuro::pkg::host_store();

    Digest d_logo = s.put(bytes_of("SOME_LOGO"));
    Digest d_kern = s.put(bytes_of("SOME_KERNEL"));

    Manifest m;
    m.package_name = "broken-pkg";
    m.entries = {
        {"/boot/logo.bin",   d_logo},
        {"/boot/kernel.bin", d_kern},
        // never inserted — references a non-existent blob
        {"/boot/config.bin", sha3_512(std::string_view{"never-inserted"})},
    };
    m.trusted_root = m.content_root();

    auto result = m.verify(s);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("/boot/config.bin") != std::string::npos);
}

// ---- 4. Tainted manifest fails the trusted_root check ----------------

TEST(manifest, verify_fails_with_tampered_trusted_root) {
    auto& s = neuro::pkg::host_store();
    Digest d = s.put(bytes_of("BIN"));

    Manifest m;
    m.package_name = "tampered";
    m.version      = "1.0";
    m.entries      = {{"file", d}};
    m.trusted_root = m.content_root();

    // Now swap an entry to a *different* blob (still in the store
    // so the has() check passes) and DON'T update trusted_root.
    Manifest evil = m;
    evil.entries[0].digest = sha3_512(std::string_view{"EVIL"});

    // Add "EVIL" to the store so has() passes.
    s.put(bytes_of("EVIL"));

    auto result = evil.verify(s);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("content_root") != std::string::npos);
}

// ---- 5. Empty manifest is well-defined --------------------------------

TEST(manifest, empty_manifest_has_well_defined_root) {
    Manifest m;
    m.package_name = "empty";
    m.version      = "0.0";
    // (no entries; trusted_root optional)
    Digest r = m.content_root();
    Digest expected = sha3_512(m.canonicalise());
    EXPECT_EQ(r, expected);
    // Empty manifest with no trusted_root still verifies successfully
    // against any store, since there are no entries to look up.
    auto& s = neuro::pkg::host_store();
    EXPECT_FALSE(m.verify(s).has_value());
}

// ---- 6. trusted_root optional: verify skips cross-check if absent -----

TEST(manifest, verify_without_trusted_root_skips_cross_check) {
    auto& s = neuro::pkg::host_store();
    Digest d = s.put(bytes_of("BIN"));

    Manifest m;
    m.package_name = "open";
    m.entries = {{"file", d}};
    // trusted_root intentionally left empty.

    EXPECT_FALSE(m.verify(s).has_value());

    // Even with an entry that we mutate post-hoc: no trusted_root
    // means there's no second line of defence. That's by design —
    // a manifest without a signature is as trustworthy as the bytes
    // you got it from.
    Manifest m2 = m;
    m2.entries[0].name = "renamed";
    EXPECT_FALSE(m2.verify(s).has_value());
}

// ---- 7. Canonical encoding is symmetric: same input → same bytes -----

TEST(manifest, canonicalise_is_byte_deterministic) {
    Manifest m;
    m.package_name = "p";
    m.version      = "v";
    auto h = sha3_512(std::string_view{"x"});
    m.entries = { {"a", h}, {"b", h} };

    auto c1 = m.canonicalise();
    auto c2 = m.canonicalise();
    EXPECT_EQ(c1.size(), c2.size());
    EXPECT_EQ(std::memcmp(c1.data(), c2.data(), c1.size()), 0);
}

// ---- 8. Canonical encoding uses length-prefix framing -----------------
//
// The canonical bytes are not the same as a naive concat
// (because each segment is length-prefixed). This catches a
// regression where the prefix framing gets removed.

TEST(manifest, canonicalise_is_not_naive_concat) {
    Manifest a;
    a.package_name = "ab";
    a.version      = "c";
    a.entries      = {};

    Manifest b;
    b.package_name = "a";
    b.version      = "bc";
    b.entries      = {};

    EXPECT_TRUE(a.content_root() != b.content_root());
}

RUN_ALL_TESTS()