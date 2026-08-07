// tests/unit/mem/page_table_test.cpp
//
// Tests for neuro::mem::RadixPageTable — the host scaffold's
// virtual→physical page mapping table. Covers:
//
//   - constants (page size, mask, base/offset helpers)
//   - default-constructed table is empty
//   - map() + translate() round-trip
//   - translate() preserves intra-page offset
//   - map() with unaligned vaddr is rounded down to page base
//   - unmap() removes a mapping and returns true; missing returns false
//   - lookup() returns the PTE with the right physical frame + perms
//   - PagePerm operator| composition
//   - size() tracks number of mapped pages
//   - flush() is a no-op on host (must not crash)

#include "neuro/mem/page_table.hpp"

#include <cstdint>

#include "../../test_framework.hpp"

using neuro::mem::kPageMask;
using neuro::mem::kPageSize;
using neuro::mem::PagePerm;
using neuro::mem::PageTable;
using neuro::mem::RadixPageTable;

// ---- 1. constants -----------------------------------------------------

TEST(page_table, page_constants) {
    EXPECT_EQ(static_cast<std::uint64_t>(0x1000), kPageSize);
    EXPECT_EQ(static_cast<std::uint64_t>(0xFFF), kPageMask);
    EXPECT_EQ(static_cast<std::size_t>(12),        neuro::mem::kPageBits);
}

// Z4.6: huge/giant page constants.
TEST(page_table, huge_and_giant_page_constants) {
    EXPECT_EQ(static_cast<std::size_t>(21),       neuro::mem::kHugePageBits);
    EXPECT_EQ(static_cast<std::uint64_t>(0x200000ULL),
              neuro::mem::kHugePageSize);    // 2 MiB
    EXPECT_EQ(static_cast<std::size_t>(30),       neuro::mem::kGiantPageBits);
    EXPECT_EQ(static_cast<std::uint64_t>(0x40000000ULL),
              neuro::mem::kGiantPageSize);   // 1 GiB
}

// Z4.5: dirty/accessed bits observable.
TEST(page_table, dirty_and_accessed_bits_clear_after_map) {
    RadixPageTable pt;
    pt.map(0x4000, 0x12345000ULL, PagePerm::Read | PagePerm::Write);
    auto pte = pt.lookup(0x4000);
    EXPECT_TRUE(pte.has_value());
    EXPECT_FALSE(pte->dirty);
    EXPECT_FALSE(pte->accessed);
}

TEST(page_table, mark_accessed_sets_accessed_bit) {
    RadixPageTable pt;
    pt.map(0x4000, 0x12345000ULL, PagePerm::Read);
    EXPECT_TRUE(pt.mark_accessed(0x4000));
    auto pte = pt.lookup(0x4000);
    EXPECT_TRUE(pte->accessed);
    EXPECT_FALSE(pte->dirty);  // unaffected
}

TEST(page_table, mark_dirty_sets_dirty_bit) {
    RadixPageTable pt;
    pt.map(0x4000, 0x12345000ULL, PagePerm::Write);
    EXPECT_TRUE(pt.mark_dirty(0x4000));
    auto pte = pt.lookup(0x4000);
    EXPECT_TRUE(pte->dirty);
    EXPECT_FALSE(pte->accessed);  // unaffected
}

TEST(page_table, mark_ops_on_unmapped_page_returns_false) {
    RadixPageTable pt;
    EXPECT_FALSE(pt.mark_accessed(0x5000));
    EXPECT_FALSE(pt.mark_dirty(0x5000));
}

TEST(page_table, mark_ops_rounds_to_page_base) {
    RadixPageTable pt;
    pt.map(0x4000, 0x12345000ULL, PagePerm::Read);
    EXPECT_TRUE(pt.mark_accessed(0x4123));   // intra-page offset
    EXPECT_TRUE(pt.mark_dirty(0x4ABC));
    auto pte = pt.lookup(0x4000);
    EXPECT_TRUE(pte->accessed);
    EXPECT_TRUE(pte->dirty);
}

TEST(page_table, page_base_and_offset) {
    EXPECT_EQ(static_cast<std::uint64_t>(0x0000),
              PageTable::page_base(0x0123));
    EXPECT_EQ(static_cast<std::uint64_t>(0x1000),
              PageTable::page_base(0x1FFF));
    EXPECT_EQ(static_cast<std::uint64_t>(0x123),
              PageTable::offset_in_page(0x1123));
    EXPECT_EQ(static_cast<std::uint64_t>(0x000),
              PageTable::offset_in_page(0x3000));
}

// ---- 2. empty table ---------------------------------------------------

TEST(page_table, empty_table) {
    RadixPageTable pt;
    EXPECT_EQ(0u, pt.size());
    EXPECT_FALSE(pt.translate(0x1000).has_value());
    EXPECT_FALSE(pt.lookup(0x1000).has_value());
}

// ---- 3. map + translate round-trip ----------------------------------

TEST(page_table, map_translate_round_trip) {
    RadixPageTable pt;
    EXPECT_TRUE(pt.map(0x4000, 0x12345000ULL, PagePerm::Read | PagePerm::Write));
    auto p = pt.translate(0x4000);
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x12345000ULL), p.value());
    EXPECT_EQ(1u, pt.size());
}

TEST(page_table, translate_preserves_intra_page_offset) {
    RadixPageTable pt;
    pt.map(0x2000, 0xCAFE0000ULL, PagePerm::Read);
    auto p = pt.translate(0x2345);
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0xCAFE0345ULL), p.value());
}

// ---- 4. unaligned vaddr rounded to page base ------------------------

TEST(page_table, map_unaligned_vaddr_rounds_down) {
    RadixPageTable pt;
    pt.map(0x1234, 0xABCD0000ULL, PagePerm::Read);
    // Both the unaligned address and its page base must translate.
    EXPECT_TRUE(pt.translate(0x1234).has_value());
    EXPECT_TRUE(pt.translate(0x1000).has_value());
    // They should map to the same physical page.
    EXPECT_EQ(pt.translate(0x1234).value() & ~kPageMask,
              pt.translate(0x1000).value() & ~kPageMask);
    EXPECT_EQ(1u, pt.size());
}

// ---- 5. unmap -------------------------------------------------------

TEST(page_table, unmap_removes_mapping) {
    RadixPageTable pt;
    pt.map(0x1000, 0x20000000ULL, PagePerm::Read);
    EXPECT_TRUE(pt.unmap(0x1000));
    EXPECT_EQ(0u, pt.size());
    EXPECT_FALSE(pt.translate(0x1000).has_value());
}

TEST(page_table, unmap_missing_returns_false) {
    RadixPageTable pt;
    EXPECT_FALSE(pt.unmap(0x1000));
    pt.map(0x1000, 0x20000000ULL, PagePerm::Read);
    EXPECT_FALSE(pt.unmap(0x9000));  // different page
    EXPECT_EQ(1u, pt.size());
}

// ---- 6. lookup returns PTE with right perm / phys ------------------

TEST(page_table, lookup_returns_pte) {
    RadixPageTable pt;
    auto perms = PagePerm::Read | PagePerm::Write | PagePerm::Exec;
    pt.map(0x3000, 0xDEAD0000ULL, perms);

    auto e = pt.lookup(0x3456);  // intra-page lookup rounds to 0x3000
    EXPECT_TRUE(e.has_value());
    EXPECT_TRUE(e->present);
    EXPECT_EQ(perms, e->perm);
    // phys stores the page-frame number = paddr >> kPageBits.
    EXPECT_EQ(static_cast<std::uint64_t>(0xDEAD0ULL), e->phys);
}

// ---- 7. PagePerm operator| composition ------------------------------

TEST(page_table, perm_composition) {
    auto p = PagePerm::Read | PagePerm::Write;
    EXPECT_TRUE((p & PagePerm::Read)  == PagePerm::Read);
    EXPECT_TRUE((p & PagePerm::Write) == PagePerm::Write);
    EXPECT_TRUE((p & PagePerm::Exec)  == PagePerm::None);

    auto q = PagePerm::User | PagePerm::Global;
    EXPECT_TRUE((q & PagePerm::User)   == PagePerm::User);
    EXPECT_TRUE((q & PagePerm::Global) == PagePerm::Global);
}

// ---- 8. size tracking ----------------------------------------------

TEST(page_table, size_tracks_unique_pages) {
    RadixPageTable pt;
    pt.map(0x1000, 0x10000000ULL, PagePerm::Read);
    pt.map(0x2000, 0x20000000ULL, PagePerm::Read);
    pt.map(0x3000, 0x30000000ULL, PagePerm::Read);
    EXPECT_EQ(3u, pt.size());

    // Re-mapping the same page updates in place (not 4 entries).
    pt.map(0x1000, 0x99990000ULL, PagePerm::Write);
    EXPECT_EQ(3u, pt.size());
    auto p = pt.translate(0x1000);
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x99990000ULL), p.value());
}

// ---- 9. flush is a no-op on host -----------------------------------

TEST(page_table, flush_does_not_crash) {
    RadixPageTable pt;
    pt.map(0x1000, 0x1000, PagePerm::Read);
    pt.flush(0x1000);
    pt.flush(0x9999);   // even for unmapped addresses
    EXPECT_EQ(1u, pt.size());
}

RUN_ALL_TESTS()