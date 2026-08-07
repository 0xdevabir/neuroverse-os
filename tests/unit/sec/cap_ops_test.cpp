// tests/unit/sec/cap_ops_test.cpp
//
// Direct unit tests for the CapOps static helpers — mint, resolve,
// attenuate, grant, duplicate, revoke — and their interaction with
// the CapabilitySpace and CapEpoch primitives.

#include "neuro/sec/cap_ops.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;
using neuro::sec::CapabilitySpace;
using neuro::sec::GrantResult;

namespace {

constexpr std::uint64_t kObj = 42;

// A bit-set with all four rights set, used as the "maximal" starting
// capability for attenuation tests.
constexpr CapRight kAll =
    CapRight::Read  | CapRight::Write |
    CapRight::Grant | CapRight::Signal;

}  // namespace

// ---- 1. mint + resolve happy path ---------------------------------

TEST(cap_ops, mint_inserts_a_resolvable_capability) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h = CapOps::mint(space, epoch, kObj, kAll, /*gen=*/1);
    EXPECT_TRUE(h != neuro::sec::kInvalidHandle);

    auto resolved = CapOps::resolve(space, epoch, h, CapRight::Read);
    EXPECT_TRUE(resolved.has_value());
    EXPECT_EQ(kObj, resolved->object_id);
    EXPECT_TRUE(resolved->has(kAll));
}

TEST(cap_ops, resolve_rejects_insufficient_rights) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h = CapOps::mint(space, epoch, kObj, CapRight::Read, 1);

    EXPECT_TRUE(CapOps::resolve(space, epoch, h, CapRight::Read).has_value());
    EXPECT_FALSE(CapOps::resolve(space, epoch, h, CapRight::Write).has_value());
    EXPECT_FALSE(CapOps::resolve(space, epoch, h, CapRight::Grant).has_value());
}

TEST(cap_ops, resolve_unknown_handle_returns_nullopt) {
    CapabilitySpace space;
    CapEpoch        epoch;
    EXPECT_FALSE(CapOps::resolve(space, epoch, 0xDEAD, CapRight::Read)
                     .has_value());
}

// ---- 2. attenuation rules -----------------------------------------

TEST(cap_ops, attenuate_accepts_subset) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h = CapOps::mint(space, epoch, kObj, kAll, 1);
    auto cap = space.lookup(h).value();

    auto narrower = CapOps::attenuate(cap, CapRight::Read | CapRight::Write);
    EXPECT_TRUE(narrower.has_value());
    EXPECT_TRUE(narrower->has(CapRight::Read));
    EXPECT_TRUE(narrower->has(CapRight::Write));
    EXPECT_FALSE(narrower->has(CapRight::Grant));
    EXPECT_FALSE(narrower->has(CapRight::Signal));
}

TEST(cap_ops, attenuate_rejects_superset) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h = CapOps::mint(space, epoch, kObj, CapRight::Read, 1);
    auto cap = space.lookup(h).value();

    // Adding Grant is a widening — must be rejected.
    auto wider = CapOps::attenuate(cap, CapRight::Read | CapRight::Grant);
    EXPECT_FALSE(wider.has_value());
}

TEST(cap_ops, attenuate_to_same_rights_returns_equal_cap) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h = CapOps::mint(space, epoch, kObj, kAll, 1);
    auto cap = space.lookup(h).value();

    auto same = CapOps::attenuate(cap, kAll);
    EXPECT_TRUE(same.has_value());
    EXPECT_TRUE(same->has(kAll));
}

// ---- 3. revoke ---------------------------------------------------

TEST(cap_ops, revoke_invalidates_every_capability) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h1 = CapOps::mint(space, epoch, 1, CapRight::Read, 1);
    const auto h2 = CapOps::mint(space, epoch, 2, CapRight::Write, 1);
    EXPECT_TRUE(h1 != neuro::sec::kInvalidHandle);
    EXPECT_TRUE(h2 != neuro::sec::kInvalidHandle);

    // Both resolve before revoke.
    EXPECT_TRUE(CapOps::resolve(space, epoch, h1, CapRight::Read).has_value());
    EXPECT_TRUE(CapOps::resolve(space, epoch, h2, CapRight::Write).has_value());

    CapOps::revoke(space, epoch);

    // Both stop resolving — but the handles themselves still exist in
    // the radix trie (revoke only bumps the epoch).
    EXPECT_FALSE(CapOps::resolve(space, epoch, h1, CapRight::Read).has_value());
    EXPECT_FALSE(CapOps::resolve(space, epoch, h2, CapRight::Write).has_value());
    EXPECT_TRUE(space.lookup(h1).has_value());
    EXPECT_TRUE(space.lookup(h2).has_value());
}

TEST(cap_ops, fresh_mint_after_revoke_resolves) {
    CapabilitySpace space;
    CapEpoch        epoch;
    const auto h_old = CapOps::mint(space, epoch, 1, CapRight::Read, 1);
    CapOps::revoke(space, epoch);

    const auto h_new = CapOps::mint(space, epoch, 2, CapRight::Read, 1);
    EXPECT_TRUE(h_new != neuro::sec::kInvalidHandle);
    EXPECT_TRUE(CapOps::resolve(space, epoch, h_new, CapRight::Read).has_value());

    // The old handle still fails (its epoch predates the revoke).
    EXPECT_FALSE(CapOps::resolve(space, epoch, h_old, CapRight::Read)
                     .has_value());
}

// ---- 4. grant cross-space ----------------------------------------

TEST(cap_ops, grant_moves_capability_to_destination_space) {
    CapabilitySpace src;
    CapabilitySpace dst;
    CapEpoch        src_epoch;
    const auto h_src =
        CapOps::mint(src, src_epoch, kObj, kAll, /*gen=*/1);

    const auto r = CapOps::grant(src, dst, src_epoch, h_src);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.handle != neuro::sec::kInvalidHandle);

    // Destination has the cap; source has lost it.
    EXPECT_TRUE(dst.lookup(r.handle).has_value());
    EXPECT_FALSE(src.lookup(h_src).has_value());
}

TEST(cap_ops, grant_take_false_duplicates_in_destination) {
    CapabilitySpace src;
    CapabilitySpace dst;
    CapEpoch        src_epoch;
    const auto h_src =
        CapOps::mint(src, src_epoch, kObj, kAll, /*gen=*/1);

    const auto r = CapOps::grant(src, dst, src_epoch, h_src,
                                 CapRight::None, /*take=*/false);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(src.lookup(h_src).has_value());       // still here
    EXPECT_TRUE(dst.lookup(r.handle).has_value());    // also here
}

TEST(cap_ops, grant_requires_grant_right_on_source) {
    CapabilitySpace src;
    CapabilitySpace dst;
    CapEpoch        src_epoch;
    // Source cap has no Grant right — grant must fail.
    const auto h_src =
        CapOps::mint(src, src_epoch, kObj, CapRight::Read, /*gen=*/1);

    const auto r = CapOps::grant(src, dst, src_epoch, h_src);
    EXPECT_FALSE(r.ok);
    // Source space is untouched on failure.
    EXPECT_TRUE(src.lookup(h_src).has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), dst.size());
}

TEST(cap_ops, grant_with_attenuation_narrows_rights) {
    CapabilitySpace src;
    CapabilitySpace dst;
    CapEpoch        src_epoch;
    const auto h_src =
        CapOps::mint(src, src_epoch, kObj, kAll, /*gen=*/1);

    const auto r = CapOps::grant(src, dst, src_epoch, h_src,
                                 CapRight::Read | CapRight::Write);
    EXPECT_TRUE(r.ok);

    auto dst_cap = dst.lookup(r.handle).value();
    EXPECT_TRUE(dst_cap.has(CapRight::Read));
    EXPECT_TRUE(dst_cap.has(CapRight::Write));
    EXPECT_FALSE(dst_cap.has(CapRight::Grant));
    EXPECT_FALSE(dst_cap.has(CapRight::Signal));
}

TEST(cap_ops, grant_with_superset_attenuation_rejected) {
    CapabilitySpace src;
    CapabilitySpace dst;
    CapEpoch        src_epoch;
    const auto h_src =
        CapOps::mint(src, src_epoch, kObj, CapRight::Read, /*gen=*/1);

    // Grant right is wider than the source — must fail.
    const auto r = CapOps::grant(src, dst, src_epoch, h_src,
                                 CapRight::Read | CapRight::Grant);
    EXPECT_FALSE(r.ok);
}

RUN_ALL_TESTS()
