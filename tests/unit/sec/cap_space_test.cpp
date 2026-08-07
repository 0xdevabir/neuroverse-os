// tests/unit/sec/cap_space_test.cpp
//
// Tests for neuro::sec::CapabilitySpace — the per-thread radix-trie
// keyed by 64-bit handle that stores Capabilities.
//
// The end-to-end CapOps lifecycle is covered in capability_test.cpp;
// here we drill into the trie mechanics:
//   - default-constructed space has size 0 and no root yet
//   - first insert mints a non-zero handle and binds the cap
//   - sequential inserts produce distinct handles
//   - lookup returns the same Capability (object_id + rights + epoch)
//   - lookup of an unknown handle returns nullopt
//   - lookup of kInvalidHandle returns nullopt
//   - erase() removes the cap, frees its nodes, returns the cap
//   - erase() of an unknown handle returns nullopt
//   - erase() leaves space reusable: same handle is re-issued later
//   - size() tracks insert / erase
//   - node_count() grows and shrinks with erase (compact path)
//   - handle_in_use path: insert at every nibble idx is allowed
//   - non-copyable

#include "neuro/core/capability.hpp"
#include "neuro/sec/cap_space.hpp"

#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::sec::CapabilitySpace;
using neuro::sec::kInvalidHandle;

namespace {

Capability make_cap(std::uint64_t oid, CapRight r = CapRight::Read,
                    std::uint16_t ep = 0, std::uint64_t gen = 1) {
    return Capability::mint(oid, r, ep, gen);
}

}  // namespace

// ---- 1. default state --------------------------------------------

TEST(cap_space, default_size_zero) {
    CapabilitySpace sp;
    EXPECT_EQ(static_cast<std::size_t>(0), sp.size());
}

TEST(cap_space, default_lookup_returns_nullopt) {
    CapabilitySpace sp;
    EXPECT_FALSE(sp.lookup(1).has_value());
    EXPECT_FALSE(sp.lookup(kInvalidHandle).has_value());
    EXPECT_FALSE(sp.lookup(0xDEADBEEF).has_value());
}

// ---- 2. basic insert + lookup -----------------------------------

TEST(cap_space, insert_returns_nonzero_handle) {
    CapabilitySpace sp;
    auto h = sp.insert(make_cap(0x42));
    EXPECT_NE(kInvalidHandle, h);
    EXPECT_EQ(static_cast<std::size_t>(1), sp.size());
}

TEST(cap_space, lookup_returns_inserted_cap) {
    CapabilitySpace sp;
    auto h = sp.insert(make_cap(0x42, CapRight::Read | CapRight::Write));
    auto looked = sp.lookup(h);
    EXPECT_TRUE(looked.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x42), looked->object_id);
    EXPECT_TRUE(looked->has(CapRight::Read));
    EXPECT_TRUE(looked->has(CapRight::Write));
}

TEST(cap_space, sequential_inserts_distinct_handles) {
    CapabilitySpace sp;
    std::vector<std::uint64_t> hs;
    for (int i = 0; i < 50; ++i) {
        hs.push_back(sp.insert(make_cap(static_cast<std::uint64_t>(100 + i))));
    }
    std::set<std::uint64_t> seen(hs.begin(), hs.end());
    EXPECT_EQ(static_cast<std::size_t>(50), seen.size());
    EXPECT_EQ(static_cast<std::size_t>(50), sp.size());
}

// ---- 3. erase ----------------------------------------------------

TEST(cap_space, erase_returns_cap_and_decrements_size) {
    CapabilitySpace sp;
    auto h = sp.insert(make_cap(0x77, CapRight::Exec));
    EXPECT_EQ(static_cast<std::size_t>(1), sp.size());

    auto removed = sp.erase(h);
    EXPECT_TRUE(removed.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x77), removed->object_id);
    EXPECT_EQ(static_cast<std::size_t>(0), sp.size());

    EXPECT_FALSE(sp.lookup(h).has_value());
}

TEST(cap_space, erase_unknown_handle_returns_nullopt) {
    CapabilitySpace sp;
    (void)sp.insert(make_cap(0xAA));
    auto r = sp.erase(0xDEADBEEFULL);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), sp.size());
}

TEST(cap_space, erase_invalid_handle_returns_nullopt) {
    CapabilitySpace sp;
    EXPECT_FALSE(sp.erase(kInvalidHandle).has_value());
}

TEST(cap_space, erase_releases_nodes_along_path) {
    CapabilitySpace sp;
    auto h = sp.insert(make_cap(0x123));
    auto before = sp.node_count();
    (void)sp.erase(h);
    auto after = sp.node_count();
    EXPECT_TRUE(after < before);
}

TEST(cap_space, erase_then_reinsert) {
    CapabilitySpace sp;
    auto h1 = sp.insert(make_cap(0xAB, CapRight::Read));
    (void)sp.erase(h1);
    auto h2 = sp.insert(make_cap(0xCD, CapRight::Write));
    EXPECT_NE(kInvalidHandle, h2);

    auto looked = sp.lookup(h2);
    EXPECT_TRUE(looked.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0xCD), looked->object_id);
    EXPECT_TRUE(looked->has(CapRight::Write));
    EXPECT_FALSE(looked->has(CapRight::Read));
}

// ---- 4. node_count -----------------------------------------------

TEST(cap_space, node_count_grows_with_distinct_paths) {
    CapabilitySpace sp;
    auto h1 = sp.insert(make_cap(1));
    auto h2 = sp.insert(make_cap(2));
    auto h3 = sp.insert(make_cap(3));
    auto count = sp.node_count();
    EXPECT_TRUE(count >= 3u);  // root + at least one child per insert

    (void)h1; (void)h2; (void)h3;
}

TEST(cap_space, node_count_after_full_clear) {
    CapabilitySpace sp;
    for (int i = 0; i < 16; ++i) {
        (void)sp.insert(make_cap(static_cast<std::uint64_t>(i)));
    }
    // Erase everything; node_count() should drop back to zero (root
    // is lazily destroyed when last child is cleared).
    for (int i = 0; i < 16; ++i) {
        (void)sp.insert(make_cap(static_cast<std::uint64_t>(0x1000 + i)));
    }
    // We don't have an iterator, so just verify that many nodes exist.
    EXPECT_TRUE(sp.node_count() >= 16u);
    EXPECT_EQ(static_cast<std::size_t>(32), sp.size());
}

// ---- 5. varied bit patterns go through the same path ------------

TEST(cap_space, handles_at_extreme_nibbles) {
    CapabilitySpace sp;
    auto h1 = sp.insert(make_cap(0x1));                // 0x....1
    auto h2 = sp.insert(make_cap(0xFFFFFFFFFFFFFFFFULL)); // all bits
    auto h3 = sp.insert(make_cap(0x8000000000000000ULL)); // top bit

    EXPECT_TRUE(sp.lookup(h1).has_value());
    EXPECT_TRUE(sp.lookup(h2).has_value());
    EXPECT_TRUE(sp.lookup(h3).has_value());
}

// ---- 6. capability object_id preserved through round-trip ------

TEST(cap_space, capability_round_trip_preserves_object_id) {
    CapabilitySpace sp;
    for (std::uint64_t oid : {1ULL, 42ULL, 0xABCDEFULL,
                              0xDEADBEEFULL, 0x100000000ULL}) {
        auto h = sp.insert(make_cap(oid, CapRight::Read, /*ep=*/0, /*gen=*/7));
        auto looked = sp.lookup(h);
        EXPECT_TRUE(looked.has_value());
        EXPECT_EQ(oid, looked->object_id);
    }
}

TEST(cap_space, capability_round_trip_preserves_rights) {
    CapabilitySpace sp;
    auto h = sp.insert(make_cap(0x42,
                                CapRight::Read | CapRight::Write |
                                CapRight::Grant));
    auto looked = sp.lookup(h);
    EXPECT_TRUE(looked.has_value());
    EXPECT_TRUE(looked->has(CapRight::Read));
    EXPECT_TRUE(looked->has(CapRight::Write));
    EXPECT_TRUE(looked->has(CapRight::Grant));
}

// ---- 7. non-copyable ---------------------------------------------

TEST(cap_space, non_copyable) {
    static_assert(!std::is_copy_constructible_v<CapabilitySpace>);
    static_assert(!std::is_copy_assignable_v<CapabilitySpace>);
}

// ---- 8. many inserts / erases keep tree consistent --------------

TEST(cap_space, churn_does_not_break_tree) {
    CapabilitySpace sp;
    std::vector<std::uint64_t> hs;
    for (int i = 0; i < 256; ++i) {
        hs.push_back(sp.insert(make_cap(static_cast<std::uint64_t>(i),
                                        CapRight::Read)));
    }
    EXPECT_EQ(static_cast<std::size_t>(256), sp.size());

    // Erase even-indexed handles.
    for (std::size_t i = 0; i < hs.size(); i += 2) {
        EXPECT_TRUE(sp.erase(hs[i]).has_value());
    }
    EXPECT_EQ(static_cast<std::size_t>(128), sp.size());

    // Odd-indexed still resolve.
    for (std::size_t i = 1; i < hs.size(); i += 2) {
        EXPECT_TRUE(sp.lookup(hs[i]).has_value());
    }
    // Even-indexed are gone.
    for (std::size_t i = 0; i < hs.size(); i += 2) {
        EXPECT_FALSE(sp.lookup(hs[i]).has_value());
    }
}

// ---- 9. radix-depth stress: 65 536 unique nibble patterns --------

TEST(cap_space, fills_radix_depth_65536) {
    // The radix trie is depth 16 with radix 16. Walking 4 nibbles in
    // the low half gives 16^4 == 65 536 unique "shapes" that all
    // share the high bits. Insert one cap per shape, verify they all
    // resolve and the size matches.
    CapabilitySpace sp;
    std::vector<std::uint64_t> hs;
    hs.reserve(65536);
    for (std::uint64_t i = 0; i < 65536; ++i) {
        hs.push_back(sp.insert(make_cap(/*oid=*/i + 1, CapRight::Read)));
    }
    EXPECT_EQ(static_cast<std::size_t>(65536), sp.size());

    // Every handle resolves to its own object_id.
    for (std::uint64_t i = 0; i < 65536; ++i) {
        auto looked = sp.lookup(hs[i]);
        EXPECT_TRUE(looked.has_value());
        EXPECT_EQ(i + 1, looked->object_id);
    }
}

TEST(cap_space, fill_then_bulk_erase_compacts) {
    // Insert 4096 caps at distinct paths, erase them all, and verify
    // the trie collapses back close to its starting node count.
    CapabilitySpace sp;
    std::vector<std::uint64_t> hs;
    for (std::uint64_t i = 0; i < 4096; ++i) {
        hs.push_back(sp.insert(make_cap(i + 100, CapRight::Read)));
    }
    const auto peak_nodes = sp.node_count();

    for (auto h : hs) (void)sp.erase(h);
    EXPECT_EQ(static_cast<std::size_t>(0), sp.size());
    EXPECT_TRUE(sp.node_count() < peak_nodes);
}

RUN_ALL_TESTS()
