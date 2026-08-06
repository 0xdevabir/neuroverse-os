// tests/unit/sec/capability_test.cpp
//
// Unit tests for the Capability / CapabilitySpace / CapEpoch / CapOps
// pipeline defined in:
//   include/neuro/core/capability.hpp
//   include/neuro/sec/cap_space.hpp
//   include/neuro/sec/epoch.hpp
//   include/neuro/sec/cap_ops.hpp
//
// Covers the full lifecycle from README §4.2:
//   mint -> grant -> attenuate -> revoke (and verifies each transition).

#include <set>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/core/capability.hpp"
#include "neuro/sec/cap_ops.hpp"

using namespace neuro;
using neuro::core::CapRight;
using neuro::sec::CapabilitySpace;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;

TEST(sec, mints_and_resolves_a_capability) {
    CapabilitySpace sp;
    CapEpoch        ep;
    auto h = CapOps::mint(sp, ep, /*oid=*/0x42,
                          CapRight::Read | CapRight::Write,
                          /*generation=*/1);
    EXPECT_NE(h, neuro::sec::kInvalidHandle);

    auto r = CapOps::resolve(sp, ep, h, CapRight::Read);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->has(CapRight::Read));
    EXPECT_TRUE(r->has(CapRight::Write));
    EXPECT_FALSE(r->has(CapRight::Exec));
}

TEST(sec, resolve_rejects_missing_rights) {
    CapabilitySpace sp;
    CapEpoch        ep;
    auto h = CapOps::mint(sp, ep, 0x42, CapRight::Read, 1);

    auto ok  = CapOps::resolve(sp, ep, h, CapRight::Read);
    auto bad = CapOps::resolve(sp, ep, h, CapRight::Write);
    EXPECT_TRUE(ok.has_value());
    EXPECT_FALSE(bad.has_value());
}

TEST(sec, attenuation_reduces_rights) {
    auto cap = core::Capability::mint(0x42,
                                      CapRight::Read | CapRight::Write,
                                      0, 1);
    auto ro = cap.attenuate(CapRight::Read);
    EXPECT_TRUE(ro.has(CapRight::Read));
    EXPECT_FALSE(ro.has(CapRight::Write));
}

TEST(sec, capops_attenuate_strict_subset) {
    auto cap = core::Capability::mint(0x42, CapRight::Read, 0, 1);
    auto narrowed = CapOps::attenuate(cap, CapRight::Read);
    EXPECT_TRUE(narrowed.has_value());

    auto rejected = CapOps::attenuate(cap, CapRight::Write);
    EXPECT_FALSE(rejected.has_value());  // rights must shrink, not grow
}

TEST(sec, grant_transfers_with_attenuation) {
    CapabilitySpace src, dst;
    CapEpoch        src_ep, dst_ep;
    auto h = CapOps::mint(src, src_ep, 0x42,
                          CapRight::Read | CapRight::Write | CapRight::Grant,
                          1);

    auto g = CapOps::grant(src, dst, src_ep, h,
                           CapRight::Read, /*take=*/true);
    EXPECT_TRUE(g.ok);
    EXPECT_NE(g.handle, neuro::sec::kInvalidHandle);

    // Source lost the handle.
    EXPECT_FALSE(CapOps::resolve(src, src_ep, h, CapRight::Read).has_value());

    // Destination has the narrowed version.
    auto r = CapOps::resolve(dst, dst_ep, g.handle, CapRight::Read);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->has(CapRight::Read));
    EXPECT_FALSE(r->has(CapRight::Write));
}

TEST(sec, grant_without_grant_right_fails) {
    CapabilitySpace src, dst;
    CapEpoch        src_ep, dst_ep;
    auto h = CapOps::mint(src, src_ep, 0x42, CapRight::Read, 1);

    // Source has only Read; no Grant. Move attempt must fail.
    auto g = CapOps::grant(src, dst, src_ep, h, CapRight::None);
    EXPECT_FALSE(g.ok);
}

TEST(sec, duplicate_does_not_require_grant_right) {
    CapabilitySpace src, dst;
    CapEpoch        src_ep, dst_ep;
    auto h = CapOps::mint(src, src_ep, 0x42, CapRight::Read, 1);

    auto dup = CapOps::duplicate(src, dst, h, CapRight::None);
    EXPECT_TRUE(dup.ok);

    // Both sides now hold the cap.
    EXPECT_TRUE(CapOps::resolve(src, src_ep, h, CapRight::Read).has_value());
    EXPECT_TRUE(CapOps::resolve(dst, dst_ep, dup.handle, CapRight::Read).has_value());

    (void)dst_ep;  // suppress unused-variable warning on -Wunused
}

TEST(sec, revocation_invalidates_all_caps) {
    CapabilitySpace sp;
    CapEpoch        ep;
    auto h = CapOps::mint(sp, ep, 0x42,
                          CapRight::Read | CapRight::Write, 1);

    EXPECT_TRUE(CapOps::resolve(sp, ep, h, CapRight::Read).has_value());

    CapOps::revoke(sp, ep);

    // After revoke, every previously-minted cap is stale.
    EXPECT_FALSE(CapOps::resolve(sp, ep, h, CapRight::Read).has_value());

    // Re-mint a new handle; it works against the new epoch.
    auto h2 = CapOps::mint(sp, ep, 0x42, CapRight::Read, 1);
    EXPECT_TRUE(CapOps::resolve(sp, ep, h2, CapRight::Read).has_value());
}

TEST(sec, capability_struct_size_is_16_bytes) {
    EXPECT_EQ(sizeof(core::Capability), 16u);
}

TEST(sec, multibyte_object_ids_and_handles) {
    CapabilitySpace sp;
    CapEpoch        ep;

    // Insert many caps; each gets a fresh handle.
    std::vector<std::uint64_t> handles;
    for (int i = 0; i < 100; ++i) {
        auto h = CapOps::mint(sp, ep,
                              static_cast<std::uint64_t>(0x1000 + i),
                              CapRight::All, 1);
        EXPECT_NE(h, neuro::sec::kInvalidHandle);
        handles.push_back(h);
    }
    EXPECT_EQ(sp.size(), 100u);

    // All handles resolve, all are distinct.
    std::set<std::uint64_t> seen;
    for (auto h : handles) {
        EXPECT_TRUE(CapOps::resolve(sp, ep, h, CapRight::Read).has_value());
        seen.insert(h);
    }
    EXPECT_EQ(seen.size(), handles.size());
}

#include <set>  // (already included above)

RUN_ALL_TESTS()