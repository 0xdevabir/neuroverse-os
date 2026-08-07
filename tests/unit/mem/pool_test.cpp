// tests/unit/mem/pool_test.cpp
//
// Unit tests for include/neuro/mem/pool.hpp.

#include <set>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/mem/pool.hpp"

using neuro::mem::Pool;

namespace {
struct Obj {
    int x;
    int y;
    Obj() : x(0), y(0) {}
    Obj(int a, int b) : x(a), y(b) {}
};
}  // namespace

TEST(pool, starts_empty_and_full_capacity) {
    Pool<Obj, 4> p;
    EXPECT_EQ(p.in_use(), 0u);
    EXPECT_EQ(p.capacity(), 4u);
    EXPECT_EQ(p.available(), 4u);
}

TEST(pool, allocate_then_deallocate_round_trip) {
    Pool<Obj, 4> p;
    auto* a = p.create(1, 2);
    auto* b = p.create(3, 4);
    EXPECT_EQ(p.in_use(), 2u);
    EXPECT_EQ(a->x, 1); EXPECT_EQ(a->y, 2);
    EXPECT_EQ(b->x, 3); EXPECT_EQ(b->y, 4);

    p.destroy(a);
    EXPECT_EQ(p.in_use(), 1u);

    p.destroy(b);
    EXPECT_EQ(p.in_use(), 0u);
    EXPECT_TRUE(p.available() == p.capacity());
}

TEST(pool, recycled_slots_reuse_memory) {
    Pool<Obj, 4> p;
    auto* a = p.create();
    auto addr_a = reinterpret_cast<std::uintptr_t>(a);
    p.destroy(a);
    auto* b = p.create();
    auto addr_b = reinterpret_cast<std::uintptr_t>(b);
    EXPECT_EQ(addr_a, addr_b);
    p.destroy(b);
}

TEST(pool, allocate_returns_null_when_exhausted) {
    Pool<Obj, 2> p;
    auto* a = p.create();
    auto* b = p.create();
    EXPECT_TRUE(a != nullptr);
    EXPECT_TRUE(b != nullptr);
    EXPECT_TRUE(p.allocate() == nullptr);
    EXPECT_EQ(p.in_use(), 2u);
    p.destroy(a);
    p.destroy(b);
}

TEST(pool, high_water_tracks_peak) {
    Pool<Obj, 8> p;
    std::vector<Obj*> live;
    for (int i = 0; i < 5; ++i) live.push_back(p.create(i, i));
    EXPECT_EQ(p.high_water(), 5u);
    for (auto* o : live) p.destroy(o);
    EXPECT_EQ(p.in_use(), 0u);
    EXPECT_EQ(p.high_water(), 5u);   // peak is sticky
}

TEST(pool, distinct_addresses_until_recycle) {
    Pool<Obj, 8> p;
    std::set<std::uintptr_t> seen;
    std::vector<Obj*> live;
    for (int i = 0; i < 8; ++i) {
        auto* o = p.create();
        seen.insert(reinterpret_cast<std::uintptr_t>(o));
        live.push_back(o);
    }
    EXPECT_EQ(seen.size(), 8u);
    for (auto* o : live) p.destroy(o);
}

TEST(pool, dealloc_null_is_noop) {
    Pool<Obj, 4> p;
    auto* a = p.create();
    EXPECT_EQ(p.in_use(), 1u);
    p.destroy(static_cast<Obj*>(nullptr));
    EXPECT_EQ(p.in_use(), 1u);
    p.destroy(a);
}

TEST(pool, reuse_after_full_drain_returns_original_addresses) {
    // Z4.3: allocate everything, drain, allocate everything again —
    // the second batch should reuse the same set of addresses.
    Pool<Obj, 4> p;
    std::vector<std::uintptr_t> first_addrs;
    std::vector<Obj*> first;
    for (int i = 0; i < 4; ++i) {
        Obj* o = p.create();
        first_addrs.push_back(reinterpret_cast<std::uintptr_t>(o));
        first.push_back(o);
    }
    for (auto* o : first) p.destroy(o);

    std::vector<std::uintptr_t> second_addrs;
    for (int i = 0; i < 4; ++i) {
        Obj* o = p.create();
        second_addrs.push_back(reinterpret_cast<std::uintptr_t>(o));
        first.push_back(o);
    }
    std::set<std::uintptr_t> first_set(first_addrs.begin(), first_addrs.end());
    std::set<std::uintptr_t> second_set(second_addrs.begin(), second_addrs.end());
    EXPECT_EQ(first_set, second_set);
    for (auto* o : first) p.destroy(o);
}

TEST(pool, dealloc_reduces_in_use_to_zero) {
    // Z4.3: in_use() decrements with every destroy; reaches 0.
    Pool<Obj, 3> p;
    Obj* a = p.create();
    Obj* b = p.create();
    Obj* c = p.create();
    EXPECT_EQ(p.in_use(), 3u);
    p.destroy(a); EXPECT_EQ(p.in_use(), 2u);
    p.destroy(b); EXPECT_EQ(p.in_use(), 1u);
    p.destroy(c); EXPECT_EQ(p.in_use(), 0u);
}

RUN_ALL_TESTS()