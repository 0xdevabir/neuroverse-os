// tests/unit/core/object_table_test.cpp
//
// Tests for neuro::core::ObjectTable<N> — a handle→pointer map keyed
// by KObject id, plus generation-tracked stale-pointer rejection.
//
// Coverage:
//   - empty table reports size 0
//   - insert() succeeds, lookup by id+gen returns the pointer
//   - insert() collision (different id, same probe slot) is rejected
//   - lookup() with wrong generation returns null (stale cap)
//   - refresh(id) syncs the slot to the object's current generation
//   - remove() takes the slot out; lookup returns null
//   - lookup_any_generation() ignores generation
//   - size() tracks inserts and removes
//   - KObject ids are unique across many allocations

#include "neuro/core/capability.hpp"
#include "neuro/core/kobject.hpp"
#include "neuro/core/object_table.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::core::KObject;
using neuro::core::KObjectKind;
using neuro::core::ObjectSlot;
using neuro::core::ObjectTable;

namespace {

// Lightweight KObject-ish stand-in: we need an explicit id to control
// the probe collision path. We can't trivially override next_id()
// from outside; instead we make a tiny subclass with an explicit
// id, which the KObject constructor accepts.
struct FakeObject : public KObject {
    FakeObject(KObjectKind k, std::uint64_t explicit_id)
        : KObject(k, explicit_id) {}
};

}  // namespace

// ---- 1. empty table -------------------------------------------------

TEST(object_table, empty_table_size_zero) {
    ObjectTable<16> t;
    EXPECT_EQ(0u, t.size());
    EXPECT_EQ(static_cast<std::size_t>(16), t.capacity());
    EXPECT_EQ(nullptr, t.lookup(1, 0));
}

// ---- 2. insert + lookup by id+gen ---------------------------------

TEST(object_table, insert_then_lookup) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::Endpoint, 5);
    EXPECT_TRUE(t.insert(&obj));
    EXPECT_EQ(1u, t.size());

    auto* p = t.lookup(5, obj.generation());
    EXPECT_EQ(&obj, p);
}

TEST(object_table, lookup_wrong_generation_returns_null) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::Endpoint, 7);
    t.insert(&obj);
    auto wrong_gen = obj.generation() + 1;
    EXPECT_EQ(nullptr, t.lookup(7, wrong_gen));
    auto right_gen = obj.generation();
    EXPECT_EQ(&obj, t.lookup(7, right_gen));
}

// ---- 3. insert collision rejected ---------------------------------

TEST(object_table, insert_collision_rejected) {
    // Probe is id % N. With N=4, ids 1 and 5 both hash to 1.
    ObjectTable<4> t;
    FakeObject a(KObjectKind::Thread, 1);
    FakeObject b(KObjectKind::Thread, 5);

    EXPECT_TRUE(t.insert(&a));
    EXPECT_FALSE(t.insert(&b));
    EXPECT_EQ(1u, t.size());
    EXPECT_EQ(&a, t.lookup(1, a.generation()));
}

// ---- 4. refresh updates stored generation -------------------------

TEST(object_table, refresh_after_retire) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::Endpoint, 9);
    t.insert(&obj);
    auto old_gen = obj.generation();

    // Retire: bump generation.
    (void)obj.retire();
    // Without refresh, a stale-gen lookup still resolves.
    EXPECT_EQ(&obj, t.lookup(9, old_gen));

    // After refresh, stale lookups fail.
    t.refresh(9);
    EXPECT_EQ(nullptr, t.lookup(9, old_gen));
    EXPECT_EQ(&obj, t.lookup(9, obj.generation()));
}

// ---- 5. remove then lookup fails ----------------------------------

TEST(object_table, remove_takes_slot_out) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::VNode, 11);
    t.insert(&obj);
    EXPECT_EQ(1u, t.size());
    EXPECT_TRUE(t.remove(11));
    EXPECT_EQ(0u, t.size());
    EXPECT_EQ(nullptr, t.lookup(11, obj.generation()));
    EXPECT_EQ(nullptr, t.lookup_any_generation(11));
}

TEST(object_table, remove_missing_returns_false) {
    ObjectTable<8> t;
    EXPECT_FALSE(t.remove(123));
}

// ---- 6. lookup_any_generation ignores gen -------------------------

TEST(object_table, lookup_any_generation_skips_gen) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::Driver, 13);
    t.insert(&obj);
    auto bogus_gen = obj.generation() + 999;
    EXPECT_EQ(nullptr, t.lookup(13, bogus_gen));
    EXPECT_EQ(&obj, t.lookup_any_generation(13));
}

// ---- 7. replace same id --------------------------------------------

TEST(object_table, replace_same_id_succeeds) {
    ObjectTable<8> t;
    FakeObject obj(KObjectKind::Endpoint, 17);
    EXPECT_TRUE(t.insert(&obj));
    // Same id, replace (should not bump count_).
    EXPECT_TRUE(t.insert(&obj));
    EXPECT_EQ(1u, t.size());
}

// ---- 8. capacity fixed --------------------------------------------

TEST(object_table, capacity_fixed) {
    ObjectTable<32> t;
    EXPECT_EQ(static_cast<std::size_t>(32), t.capacity());
}

// ---- 9. KObject ids unique across many allocations ---------------

TEST(object_table, kobject_ids_unique) {
    constexpr int N = 50;
    std::vector<std::unique_ptr<FakeObject>> objs;
    objs.reserve(N);
    for (int i = 0; i < N; ++i) {
        objs.push_back(std::make_unique<FakeObject>(
            KObjectKind::Endpoint,
            KObject::next_id() + static_cast<std::uint64_t>(i) + 1));
    }
    // All ids must be unique.
    std::vector<std::uint64_t> ids;
    for (auto& o : objs) ids.push_back(o->id());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            EXPECT_NE(ids[i], ids[j]);
        }
    }
    EXPECT_EQ(KObjectKind::Endpoint, objs[0]->kind());
}

// ---- 10. many distinct ids coexist --------------------------------

TEST(object_table, many_ids_resolve) {
    ObjectTable<128> t;
    constexpr int N = 64;
    std::vector<std::unique_ptr<FakeObject>> objs;
    objs.reserve(N);
    for (int i = 0; i < N; ++i) {
        objs.push_back(std::make_unique<FakeObject>(
            KObjectKind::Thread,
            KObject::next_id() + static_cast<std::uint64_t>(i) + 1));
    }
    for (auto& o : objs) {
        EXPECT_TRUE(t.insert(o.get()));
    }
    EXPECT_EQ(static_cast<std::size_t>(N), t.size());

    for (auto& o : objs) {
        EXPECT_EQ(o.get(), t.lookup(o->id(), o->generation()));
    }
}

RUN_ALL_TESTS()
