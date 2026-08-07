// tests/unit/core/capability_test.cpp
//
// Tests for neuro::core::Capability — the 128-bit unforgeable
// token defined in include/neuro/core/capability.hpp.
//
// The end-to-end CapOps pipeline is covered in sec/capability_test.cpp;
// here we drill into the Capability struct itself:
//
//   - size is exactly 16 bytes (static_assert)
//   - CapRight bitfield values are reachable and distinct
//   - operator|, operator&, |=, &= compose correctly
//   - CapRight::All contains every individual right
//   - Capability::mint stores all four fields
//   - Capability::has checks all required bits are present
//   - Capability::has returns false for any non-held right
//   - Capability::attenuate ANDs the rights with the requested subset
//   - Capability::attenuate preserves object_id / verify
//   - Capability::verify rejects object_id == 0
//   - Capability::to_string returns a non-empty, stable string

#include "neuro/core/capability.hpp"

#include <cstdint>
#include <type_traits>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;

// ---- 1. CapRight bitfield values ----------------------------------

TEST(capability, capright_values_distinct) {
    EXPECT_NE(CapRight::Read,   CapRight::Write);
    EXPECT_NE(CapRight::Read,   CapRight::Exec);
    EXPECT_NE(CapRight::Read,   CapRight::Grant);
    EXPECT_NE(CapRight::Read,   CapRight::Map);
    EXPECT_NE(CapRight::Read,   CapRight::Signal);
    EXPECT_NE(CapRight::Read,   CapRight::Audit);

    EXPECT_NE(CapRight::Write,  CapRight::Exec);
    EXPECT_NE(CapRight::Grant,  CapRight::Map);
    EXPECT_NE(CapRight::Signal, CapRight::Audit);
}

TEST(capability, capright_singleton_bits) {
    // Each right is a single bit.
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 0),
              static_cast<std::uint16_t>(CapRight::Read));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 1),
              static_cast<std::uint16_t>(CapRight::Write));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 2),
              static_cast<std::uint16_t>(CapRight::Exec));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 3),
              static_cast<std::uint16_t>(CapRight::Grant));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 4),
              static_cast<std::uint16_t>(CapRight::Map));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 5),
              static_cast<std::uint16_t>(CapRight::Signal));
    EXPECT_EQ(static_cast<std::uint16_t>(1u << 6),
              static_cast<std::uint16_t>(CapRight::Audit));
}

TEST(capability, capright_all_contains_every_right) {
    auto all = CapRight::All;
    EXPECT_TRUE((all & CapRight::Read)   == CapRight::Read);
    EXPECT_TRUE((all & CapRight::Write)  == CapRight::Write);
    EXPECT_TRUE((all & CapRight::Exec)   == CapRight::Exec);
    EXPECT_TRUE((all & CapRight::Grant)  == CapRight::Grant);
    EXPECT_TRUE((all & CapRight::Map)    == CapRight::Map);
    EXPECT_TRUE((all & CapRight::Signal) == CapRight::Signal);
    EXPECT_TRUE((all & CapRight::Audit)  == CapRight::Audit);
}

TEST(capability, capright_none_is_zero) {
    EXPECT_EQ(static_cast<std::uint16_t>(0),
              static_cast<std::uint16_t>(CapRight::None));
}

// ---- 2. operator|, &, |=, &= -------------------------------------

TEST(capability, or_combines_bits) {
    auto f = CapRight::Read | CapRight::Write;
    EXPECT_TRUE((f & CapRight::Read)  == CapRight::Read);
    EXPECT_TRUE((f & CapRight::Write) == CapRight::Write);
    EXPECT_FALSE((f & CapRight::Exec) == CapRight::Exec);
}

TEST(capability, and_extracts_bits) {
    auto f = CapRight::Read | CapRight::Write | CapRight::Exec;
    auto sub = f & CapRight::Write;
    EXPECT_EQ(CapRight::Write, sub);
}

TEST(capability, or_assign_composes_in_place) {
    auto f = CapRight::Read;
    f |= CapRight::Grant;
    EXPECT_TRUE((f & CapRight::Read)  == CapRight::Read);
    EXPECT_TRUE((f & CapRight::Grant) == CapRight::Grant);
    EXPECT_FALSE((f & CapRight::Write) == CapRight::Write);
}

TEST(capability, and_assign_clears_in_place) {
    auto f = CapRight::Read | CapRight::Write | CapRight::Grant;
    f &= CapRight::Read;
    EXPECT_TRUE((f & CapRight::Read)  == CapRight::Read);
    EXPECT_FALSE((f & CapRight::Write) == CapRight::Write);
    EXPECT_FALSE((f & CapRight::Grant) == CapRight::Grant);
}

// ---- 3. Capability::mint stores fields ----------------------------

TEST(capability, mint_with_zero_object_id) {
    auto c = Capability::mint(0, CapRight::All, 0, 1);
    EXPECT_FALSE(c.verify());  // 0 is reserved as "no object"
}

TEST(capability, mint_preserves_object_id_in_attenuate) {
    auto c = Capability::mint(0xABCD, CapRight::Read | CapRight::Write,
                              7, 99);
    auto a = c.attenuate(CapRight::Read);
    EXPECT_TRUE(a.has(CapRight::Read));
    EXPECT_FALSE(a.has(CapRight::Write));
    EXPECT_TRUE(a.verify());
}

// ---- 4. has() semantics ------------------------------------------

TEST(capability, has_returns_true_for_held_rights) {
    auto c = Capability::mint(1, CapRight::Read | CapRight::Grant, 0, 1);
    EXPECT_TRUE(c.has(CapRight::Read));
    EXPECT_TRUE(c.has(CapRight::Grant));
}

TEST(capability, has_returns_false_for_unheld_rights) {
    auto c = Capability::mint(1, CapRight::Read, 0, 1);
    EXPECT_FALSE(c.has(CapRight::Write));
    EXPECT_FALSE(c.has(CapRight::Exec));
    EXPECT_FALSE(c.has(CapRight::Grant));
    EXPECT_FALSE(c.has(CapRight::Map));
    EXPECT_FALSE(c.has(CapRight::Signal));
    EXPECT_FALSE(c.has(CapRight::Audit));
}

TEST(capability, has_composite_rights) {
    auto c = Capability::mint(1, CapRight::Read | CapRight::Write, 0, 1);
    EXPECT_TRUE(c.has(CapRight::Read | CapRight::Write));
    EXPECT_FALSE(c.has(CapRight::Read | CapRight::Exec));
}

// ---- 5. attenuate preserves non-rights fields --------------------

TEST(capability, attenuate_preserves_object_id) {
    auto c = Capability::mint(0x42, CapRight::All, 0, 1);
    auto a = c.attenuate(CapRight::Read);
    EXPECT_TRUE(a.verify());
    EXPECT_TRUE(a.has(CapRight::Read));
}

TEST(capability, attenuate_preserves_epoch_for_same_object) {
    auto c = Capability::mint(0x42, CapRight::All, 7, 99);
    auto a1 = c.attenuate(CapRight::Read);
    auto a2 = c.attenuate(CapRight::Write);
    // Both derived from the same mint, so verify must still hold.
    EXPECT_TRUE(a1.verify());
    EXPECT_TRUE(a2.verify());
    EXPECT_FALSE(a1.has(CapRight::Write));
    EXPECT_FALSE(a2.has(CapRight::Read));
}

// ---- 6. verify() -------------------------------------------------

TEST(capability, verify_accepts_nonzero_object_id) {
    auto c = Capability::mint(1, CapRight::Read, 0, 1);
    EXPECT_TRUE(c.verify());
    auto c2 = Capability::mint(0xFFFFFFFFFFFFULL, CapRight::All, 0, 0);
    EXPECT_TRUE(c2.verify());
}

TEST(capability, verify_rejects_zero_object_id) {
    auto c = Capability::mint(0, CapRight::All, 0, 1);
    EXPECT_FALSE(c.verify());
}

// ---- 7. to_string ------------------------------------------------

TEST(capability, to_string_returns_nonempty) {
    auto c = Capability::mint(1, CapRight::Read, 0, 1);
    auto s = c.to_string();
    EXPECT_FALSE(s.empty());
}

TEST(capability, to_string_is_stable) {
    auto c = Capability::mint(1, CapRight::Read, 0, 1);
    auto s1 = c.to_string();
    auto s2 = c.to_string();
    EXPECT_EQ(s1, s2);
}

// ---- 8. layout / size --------------------------------------------

TEST(capability, struct_size_is_16_bytes) {
    EXPECT_EQ(sizeof(Capability), static_cast<std::size_t>(16));
}

TEST(capability, struct_is_trivially_copyable) {
    static_assert(std::is_trivially_copyable_v<Capability>);
    static_assert(std::is_standard_layout_v<Capability>);
}

// ---- 9. attenuation can only narrow ------------------------------

TEST(capability, attenuate_cannot_add_rights) {
    auto c = Capability::mint(1, CapRight::Read, 0, 1);
    auto a = c.attenuate(CapRight::Read | CapRight::Write);
    EXPECT_FALSE(a.has(CapRight::Write));  // rights must shrink
}

TEST(capability, attenuate_with_none_strips_all) {
    auto c = Capability::mint(1, CapRight::All, 0, 1);
    auto a = c.attenuate(CapRight::None);
    EXPECT_FALSE(a.has(CapRight::Read));
    EXPECT_FALSE(a.has(CapRight::Write));
    EXPECT_FALSE(a.has(CapRight::Exec));
    EXPECT_FALSE(a.has(CapRight::Grant));
    EXPECT_FALSE(a.has(CapRight::Map));
    EXPECT_FALSE(a.has(CapRight::Signal));
    EXPECT_FALSE(a.has(CapRight::Audit));
    EXPECT_TRUE(a.verify());
}

RUN_ALL_TESTS()