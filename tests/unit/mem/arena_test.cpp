// tests/unit/mem/arena_test.cpp
//
// Unit tests for include/neuro/mem/arena.hpp.

#include <cstring>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/mem/arena.hpp"

using neuro::mem::Arena;

TEST(arena, allocate_advances_used) {
    Arena a(1024);
    EXPECT_EQ(a.used(), 0u);
    EXPECT_EQ(a.remaining(), 1024u);

    auto* p = a.allocate(64, 16);
    EXPECT_TRUE(p != nullptr);
    EXPECT_TRUE(a.used() >= 64u);
    EXPECT_TRUE(a.remaining() < 1024u);

    // Touch the bytes to make sure they're writable.
    std::memset(p, 0xAB, 64);
    EXPECT_EQ(static_cast<unsigned char>(p[0]), 0xABu);
}

TEST(arena, alignment_is_respected) {
    Arena a(1024);
    auto* p1 = a.allocate(1, 1);             // may not be aligned to 16
    auto* p2 = a.allocate(1, 16);            // must be 16-byte aligned
    auto addr = reinterpret_cast<std::uintptr_t>(p2);
    EXPECT_EQ(addr % 16, 0u);
    auto* p3 = a.allocate(1, 64);            // must be 64-byte aligned
    auto addr3 = reinterpret_cast<std::uintptr_t>(p3);
    EXPECT_EQ(addr3 % 64, 0u);
    (void)p1;
}

TEST(arena, reset_releases_everything) {
    Arena a(1024);
    auto cp = a.save();
    (void)a.allocate(512);
    EXPECT_TRUE(a.used() >= 512u);

    a.reset();
    EXPECT_EQ(a.used(), 0u);
    EXPECT_TRUE(a.empty());

    (void)cp;
}

TEST(arena, reset_to_rolls_back_to_checkpoint) {
    Arena a(1024);
    auto cp_before = a.save();
    (void)a.allocate(128);              // [0, 128)
    auto cp_middle = a.save();
    (void)a.allocate(256);              // [128, 384)
    EXPECT_TRUE(a.used() >= 384u);

    a.reset_to(cp_middle);        // undo the second allocation
    EXPECT_TRUE(a.used() < 384u);

    // The earlier checkpoint is still valid.
    a.reset_to(cp_before);
    EXPECT_EQ(a.used(), 0u);
}

TEST(arena, reset_to_only_rewinds) {
    Arena a(1024);
    (void)a.allocate(64);
    auto cp = a.save();
    (void)a.allocate(64);
    // Resetting to a checkpoint ahead of cursor should be a no-op.
    a.reset_to(cp);
    EXPECT_TRUE(a.used() >= 64u);
}

TEST(arena, reset_to_checkpoint_round_trip_reuses_address) {
    // Z4.1: allocate, save checkpoint, allocate more, reset_to(cp),
    // then allocate the same size — the new address must match the
    // second allocation's address (since the first allocation sits BEFORE
    // the checkpoint and is unaffected).
    Arena a(1024);
    (void)a.allocate(64, 16);                 // stable prefix
    auto cp = a.save();
    auto* p1 = a.allocate(128, 16);           // live allocation
    (void)a.allocate(256, 16);                // will be undone
    a.reset_to(cp);
    auto* p2 = a.allocate(128, 16);
    EXPECT_EQ(p1, p2);
}

TEST(arena, pointer_offset_within_arena_stable_after_reset) {
    // Z4.2: a pointer captured before reset must still point to a
    // valid offset within the arena after reset_to.
    Arena a(1024);
    auto* p1 = a.allocate(64, 16);
    auto* p2 = a.allocate(64, 16);
    auto cp = a.save();
    (void)a.allocate(64, 16);
    a.reset_to(cp);
    // The offset of p1 (relative to base) is unchanged.
    auto off_p1 = static_cast<std::size_t>(
        reinterpret_cast<std::uintptr_t>(p1) -
        reinterpret_cast<std::uintptr_t>(a.base()));
    EXPECT_EQ(off_p1, 0u);  // first allocation
    auto off_p2 = static_cast<std::size_t>(
        reinterpret_cast<std::uintptr_t>(p2) -
        reinterpret_cast<std::uintptr_t>(a.base()));
    EXPECT_TRUE(off_p2 >= 64u);
    EXPECT_TRUE(off_p2 <  128u);
}

TEST(arena, pointer_stability_across_full_reset) {
    // Z4.2 follow-up: after reset() (full release), the previously
    // captured pointer p1 still lives within the arena's address range,
    // even though the memory is logically freed. The pointer arithmetic
    // remains valid.
    Arena a(1024);
    auto* p1 = a.allocate(200, 16);
    auto off_p1 = reinterpret_cast<std::uintptr_t>(p1) -
                  reinterpret_cast<std::uintptr_t>(a.base());
    a.reset();
    EXPECT_EQ(a.used(), 0u);
    auto* p1_after = reinterpret_cast<std::byte*>(
        reinterpret_cast<std::uintptr_t>(a.base()) + off_p1);
    EXPECT_EQ(p1, p1_after);  // same address; byte is now reusable
    auto* p2 = a.allocate(200, 16);
    EXPECT_EQ(p1, p2);
}

TEST(arena, make_T_constructs_in_place) {
    struct Point { int x; int y; Point(int a, int b) : x(a), y(b) {} };
    Arena a(1024);
    auto* p = a.make<Point>(3, 4);
    EXPECT_EQ(p->x, 3);
    EXPECT_EQ(p->y, 4);
}

TEST(arena, allocate_throws_when_full) {
    Arena a(64);
    (void)a.allocate(64);
    bool threw = false;
    try {
        (void)a.allocate(1);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

RUN_ALL_TESTS()