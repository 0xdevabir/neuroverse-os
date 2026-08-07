// tests/unit/mem/vma_tree_test.cpp
//
// Tests for neuro::mem::VMATree — a sorted interval registry used by
// each userspace process to track its address-space layout.
//
// Coverage:
//   - default-constructed tree is empty
//   - insert() succeeds for non-overlapping VMAs
//   - insert() rejects overlapping VMAs (returns false)
//   - insert() rejects zero-length intervals
//   - find() returns the VMA containing an address
//   - find() returns nullopt for addresses outside any VMA
//   - find() handles boundary addresses (start inclusive, end exclusive)
//   - erase() by exact start address
//   - erase() of missing start returns false
//   - size() and total_size() match inserted intervals
//   - intervals() returns entries in address order

#include "neuro/mem/vma_tree.hpp"

#include <cstdint>

#include "../../test_framework.hpp"

using neuro::mem::VMA;
using neuro::mem::VMATree;

namespace {

VMA vma(std::uint64_t s, std::uint64_t e, std::uint32_t r = 0,
        std::uint64_t b = 0) {
    VMA v{};
    v.start   = s;
    v.end     = e;
    v.rights  = r;
    v.backing = b;
    return v;
}

}  // namespace

// ---- 1. empty tree --------------------------------------------------

TEST(vma_tree, empty_tree) {
    VMATree t;
    EXPECT_EQ(0u, t.size());
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(0u, t.total_size());
    EXPECT_TRUE(t.find(0).has_value() == false);
}

// ---- 2. insert succeeds --------------------------------------------

TEST(vma_tree, insert_succeeds) {
    VMATree t;
    EXPECT_TRUE(t.insert(vma(0x1000, 0x2000)));
    EXPECT_TRUE(t.insert(vma(0x3000, 0x4000)));
    EXPECT_EQ(2u, t.size());
}

// ---- 3. insert rejects overlapping ---------------------------------

TEST(vma_tree, insert_rejects_overlap) {
    VMATree t;
    EXPECT_TRUE(t.insert(vma(0x1000, 0x3000)));

    // Strictly inside.
    EXPECT_FALSE(t.insert(vma(0x1500, 0x2000)));
    // Partial overlap on the right.
    EXPECT_FALSE(t.insert(vma(0x2000, 0x4000)));
    // Partial overlap on the left.
    EXPECT_FALSE(t.insert(vma(0x0000, 0x1500)));
    // Identical.
    EXPECT_FALSE(t.insert(vma(0x1000, 0x3000)));

    EXPECT_EQ(1u, t.size());
}

// ---- 4. insert rejects zero / inverted intervals --------------------

TEST(vma_tree, insert_rejects_zero_length) {
    VMATree t;
    EXPECT_FALSE(t.insert(vma(0x1000, 0x1000)));
    EXPECT_FALSE(t.insert(vma(0x2000, 0x1000)));
    EXPECT_EQ(0u, t.size());
}

// ---- 5. find() hits + misses ----------------------------------------

TEST(vma_tree, find_hit_and_miss) {
    VMATree t;
    t.insert(vma(0x1000, 0x2000));
    t.insert(vma(0x4000, 0x5000));
    t.insert(vma(0xA000, 0xB000));

    auto v = t.find(0x1500);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x1000), v->start);
    EXPECT_EQ(static_cast<std::uint64_t>(0x2000), v->end);

    auto v2 = t.find(0xA500);
    EXPECT_TRUE(v2.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0xA000), v2->start);

    // Gap between intervals.
    EXPECT_FALSE(t.find(0x3000).has_value());
    EXPECT_FALSE(t.find(0xFFFF).has_value());
    EXPECT_FALSE(t.find(0x0000).has_value());
}

// ---- 6. boundary addresses (start inclusive, end exclusive) ---------

TEST(vma_tree, boundary_inclusive_start_exclusive_end) {
    VMATree t;
    t.insert(vma(0x1000, 0x2000));

    // start is inclusive.
    EXPECT_TRUE(t.find(0x1000).has_value());
    // end is exclusive.
    EXPECT_FALSE(t.find(0x2000).has_value());
    // just before end is inside.
    EXPECT_TRUE(t.find(0x1FFF).has_value());
}

// ---- 7. erase by start ---------------------------------------------

TEST(vma_tree, erase_by_start) {
    VMATree t;
    t.insert(vma(0x1000, 0x2000));
    t.insert(vma(0x3000, 0x4000));

    EXPECT_TRUE(t.erase(0x1000));
    EXPECT_EQ(1u, t.size());
    EXPECT_FALSE(t.find(0x1000).has_value());
    EXPECT_TRUE(t.find(0x3000).has_value());
}

TEST(vma_tree, erase_missing_returns_false) {
    VMATree t;
    t.insert(vma(0x1000, 0x2000));
    EXPECT_FALSE(t.erase(0x9999));
    EXPECT_FALSE(t.erase(0x1500));  // wrong start
    EXPECT_EQ(1u, t.size());
}

// ---- 8. total_size -------------------------------------------------

TEST(vma_tree, total_size_sums) {
    VMATree t;
    t.insert(vma(0x0000, 0x1000));     // 0x1000
    t.insert(vma(0x2000, 0x3000));     // 0x1000
    t.insert(vma(0x4000, 0x4000 + 0x1234));  // 0x1234
    EXPECT_EQ(0x1000u + 0x1000u + 0x1234u, t.total_size());
}

// ---- 9. intervals() in address order --------------------------------

TEST(vma_tree, intervals_in_address_order) {
    VMATree t;
    t.insert(vma(0x9000, 0xA000));
    t.insert(vma(0x1000, 0x2000));
    t.insert(vma(0x5000, 0x6000));
    t.insert(vma(0x3000, 0x4000));

    const auto& v = t.intervals();
    EXPECT_EQ(4u, v.size());
    EXPECT_EQ(static_cast<std::uint64_t>(0x1000), v[0].start);
    EXPECT_EQ(static_cast<std::uint64_t>(0x3000), v[1].start);
    EXPECT_EQ(static_cast<std::uint64_t>(0x5000), v[2].start);
    EXPECT_EQ(static_cast<std::uint64_t>(0x9000), v[3].start);
}

// ---- 10. erase all → empty ------------------------------------------

TEST(vma_tree, erase_all_returns_to_empty) {
    VMATree t;
    t.insert(vma(0x1000, 0x2000));
    t.insert(vma(0x3000, 0x4000));
    EXPECT_TRUE(t.erase(0x1000));
    EXPECT_TRUE(t.erase(0x3000));
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(0u, t.total_size());
}

// ---- 11. VMA contains / size helpers --------------------------------

TEST(vma_tree, vma_contains_and_size) {
    VMA v{};
    v.start = 0x1000;
    v.end   = 0x2000;
    EXPECT_TRUE(v.contains(0x1000));
    EXPECT_TRUE(v.contains(0x1FFF));
    EXPECT_FALSE(v.contains(0x2000));
    EXPECT_FALSE(v.contains(0x0FFF));
    EXPECT_EQ(static_cast<std::size_t>(0x1000), v.size());
}

RUN_ALL_TESTS()
