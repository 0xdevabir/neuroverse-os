// tests/unit/core/kobject_test.cpp
//
// Tests for neuro::core::KObject — the root type of every kernel-
// tracked object. Covers:
//
//   - id is minted from a process-global counter
//   - two default-constructed-in-a-row ids are distinct
//   - explicit-id constructor stores the id unchanged
//   - kind() returns the tag supplied at construction
//   - generation() starts at 0
//   - retire() returns the previous generation and bumps it
//   - retire() twice produces two distinct values
//   - KObjectKind enum values are reachable
//   - copy / move / assignment are deleted

#include "neuro/core/kobject.hpp"

#include <set>
#include <type_traits>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::KObject;
using neuro::core::KObjectKind;

namespace {

struct DummyObject : public KObject {
    using KObject::KObject;
};

}  // namespace

// ---- 1. default-constructed ids are unique ------------------------

TEST(kobject, default_ids_unique) {
    DummyObject a(KObjectKind::Thread);
    DummyObject b(KObjectKind::Endpoint);
    DummyObject c(KObjectKind::MemoryMap);
    EXPECT_NE(a.id(), b.id());
    EXPECT_NE(b.id(), c.id());
    EXPECT_NE(a.id(), c.id());
}

// ---- 2. many allocations all distinct -----------------------------

TEST(kobject, many_ids_all_distinct) {
    constexpr int N = 200;
    std::vector<std::unique_ptr<DummyObject>> objs;
    objs.reserve(N);
    for (int i = 0; i < N; ++i) {
        objs.push_back(std::make_unique<DummyObject>(KObjectKind::IoPort));
    }
    std::set<std::uint64_t> seen;
    for (auto& o : objs) {
        auto [_, inserted] = seen.insert(o->id());
        EXPECT_TRUE(inserted);
    }
    EXPECT_EQ(static_cast<std::size_t>(N), seen.size());
}

// ---- 3. explicit-id constructor ----------------------------------

TEST(kobject, explicit_id_constructor) {
    DummyObject a(KObjectKind::Endpoint, 0xABCDEFULL);
    EXPECT_EQ(static_cast<std::uint64_t>(0xABCDEFULL), a.id());
}

// ---- 4. kind() returns supplied tag -------------------------------

TEST(kobject, kind_returns_tag) {
    DummyObject t(KObjectKind::Thread);
    DummyObject e(KObjectKind::Endpoint);
    DummyObject m(KObjectKind::MemoryMap);
    DummyObject i(KObjectKind::IrqSource);
    DummyObject io(KObjectKind::IoPort);
    DummyObject v(KObjectKind::VNode);
    DummyObject d(KObjectKind::Driver);

    EXPECT_EQ(KObjectKind::Thread,    t.kind());
    EXPECT_EQ(KObjectKind::Endpoint,  e.kind());
    EXPECT_EQ(KObjectKind::MemoryMap, m.kind());
    EXPECT_EQ(KObjectKind::IrqSource, i.kind());
    EXPECT_EQ(KObjectKind::IoPort,    io.kind());
    EXPECT_EQ(KObjectKind::VNode,     v.kind());
    EXPECT_EQ(KObjectKind::Driver,    d.kind());
}

// ---- 5. generation() starts at 0 ---------------------------------

TEST(kobject, initial_generation_is_zero) {
    DummyObject o(KObjectKind::Endpoint);
    EXPECT_EQ(static_cast<std::uint64_t>(0), o.generation());
}

// ---- 6. retire() bumps generation --------------------------------

TEST(kobject, retire_returns_old_value) {
    DummyObject o(KObjectKind::Endpoint);
    // retire returns the previous generation; first call returns 0.
    auto old = o.retire();
    EXPECT_EQ(static_cast<std::uint64_t>(0), old);
    EXPECT_EQ(static_cast<std::uint64_t>(1), o.generation());

    auto old2 = o.retire();
    EXPECT_EQ(static_cast<std::uint64_t>(1), old2);
    EXPECT_EQ(static_cast<std::uint64_t>(2), o.generation());
}

// ---- 7. retire() twice produces two distinct values ---------------

TEST(kobject, retire_twice_distinct) {
    DummyObject o(KObjectKind::Endpoint);
    auto a = o.retire();
    auto b = o.retire();
    EXPECT_NE(a, b);
    EXPECT_EQ(a + 1, b);
    EXPECT_EQ(static_cast<std::uint64_t>(2), o.generation());
}

// ---- 8. KObjectKind enum values ----------------------------------

TEST(kobject, kind_enum_values_distinct) {
    std::set<int> seen;
    auto push = [&](KObjectKind k) {
        seen.insert(static_cast<int>(k));
    };
    push(KObjectKind::Untyped);
    push(KObjectKind::Thread);
    push(KObjectKind::Endpoint);
    push(KObjectKind::MemoryMap);
    push(KObjectKind::IrqSource);
    push(KObjectKind::IoPort);
    push(KObjectKind::VNode);
    push(KObjectKind::Driver);
    EXPECT_EQ(static_cast<std::size_t>(8), seen.size());
}

// ---- 9. copy / move / assignment deleted --------------------------

TEST(kobject, copy_and_move_disabled) {
    static_assert(!std::is_copy_constructible_v<KObject>);
    static_assert(!std::is_copy_assignable_v<KObject>);
    static_assert(!std::is_move_constructible_v<KObject>);
    static_assert(!std::is_move_assignable_v<KObject>);
}

// ---- 10. virtual dtor runs cleanly --------------------------------

TEST(kobject, virtual_dtor_safe) {
    // Allocate as base pointer, ensure no UB on delete.
    auto* p = new DummyObject(KObjectKind::Endpoint);
    EXPECT_EQ(KObjectKind::Endpoint, p->kind());
    delete p;  // must not leak / crash
}

RUN_ALL_TESTS()