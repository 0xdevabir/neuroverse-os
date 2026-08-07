// tests/unit/fs/ramfs_test.cpp
//
// Z6.10 — unit tests for neuro::fs::RamFS, a fixed-capacity
// in-memory file system that evicts the oldest file by last-access
// tick when the byte budget is exceeded.

#include "neuro/fs/ramfs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::ErrorKind;
using neuro::fs::FileType;
using neuro::fs::OpenFlags;
using neuro::fs::RamFS;
using neuro::fs::RamVNode;

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

}  // namespace

// ---- basic RamVNode contract ----------------------------------------

TEST(ramfs, vnode_starts_empty) {
    RamVNode node(1, "/e");
    auto stat = node.stat();
    EXPECT_TRUE(stat.has_value());
    EXPECT_EQ(FileType::Regular, stat->type);
    EXPECT_EQ(static_cast<std::uint64_t>(0), stat->size);
    EXPECT_EQ(static_cast<std::uint64_t>(0), node.size_bytes());
}

TEST(ramfs, vnode_write_and_read_round_trip) {
    RamVNode node(1, "/f");
    auto w = node.write(0, bytes("hello"));
    EXPECT_TRUE(w.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), *w);
    EXPECT_EQ(static_cast<std::size_t>(5), node.size_bytes());

    std::array<std::byte, 5> buf{};
    auto r = node.read(0, buf);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), *r);
    EXPECT_EQ(static_cast<std::uint8_t>('h'), static_cast<std::uint8_t>(buf[0]));
    EXPECT_EQ(static_cast<std::uint8_t>('o'), static_cast<std::uint8_t>(buf[4]));
}

TEST(ramfs, vnode_truncate_shrinks) {
    RamVNode node(1, "/f");
    node.write(0, bytes("abcdef"));
    EXPECT_TRUE(node.truncate(3).has_value());
    EXPECT_EQ(static_cast<std::size_t>(3), node.size_bytes());
}

// ---- RamFS: open + write_charging ----------------------------------

TEST(ramfs, open_create_mints_handle) {
    RamFS fs(/*byte_capacity=*/0);
    auto opened = fs.open("/x", OpenFlags::Create);
    EXPECT_TRUE(opened.has_value());
    EXPECT_EQ(std::string("/x"), opened->path);
    EXPECT_EQ(static_cast<std::size_t>(1), fs.size());
    EXPECT_EQ(static_cast<std::size_t>(0), fs.bytes_used());
}

TEST(ramfs, write_charging_increments_bytes_used) {
    RamFS fs(/*byte_capacity=*/0);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());

    auto w = fs.write_charging("/x", 0, bytes("hello"));
    EXPECT_TRUE(w.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), *w);
    EXPECT_EQ(static_cast<std::size_t>(5), fs.bytes_used());
}

TEST(ramfs, write_charging_in_place_does_not_charge) {
    RamFS fs(/*byte_capacity=*/0);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("0123456789")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(10), fs.bytes_used());

    // In-place write (offset 0, length 5 over 10 bytes) charges nothing.
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("HELLO")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(10), fs.bytes_used());
}

TEST(ramfs, write_charging_grow_charges_delta) {
    RamFS fs(/*byte_capacity=*/0);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("abc")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(3), fs.bytes_used());

    // Grow to 8 bytes; delta = 5.
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("abcdefgh")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(8), fs.bytes_used());
}

// ---- Eviction ---------------------------------------------------------

TEST(ramfs, evict_oldest_when_capacity_exceeded) {
    // Capacity = 6 bytes. First file = 5 bytes; second write would
    // push to 10, so the first file should be evicted.
    RamFS fs(/*byte_capacity=*/6);
    EXPECT_TRUE(fs.open("/a", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/a", 0, bytes("12345")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), fs.bytes_used());

    EXPECT_TRUE(fs.open("/b", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/b", 0, bytes("ABCDEF")).has_value());
    // /a should have been evicted to make room.
    EXPECT_EQ(static_cast<std::size_t>(6), fs.bytes_used());
    EXPECT_EQ(static_cast<std::size_t>(1), fs.size());
    EXPECT_FALSE(fs.lookup("/a").has_value());
    EXPECT_TRUE(fs.lookup("/b").has_value());
}

TEST(ramfs, evict_multiple_to_fit) {
    // Capacity = 4 bytes; populate 4 one-byte files (4 bytes used).
    RamFS fs(/*byte_capacity=*/4);
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(fs.open("/f" + std::to_string(i), OpenFlags::Create)
                        .has_value());
        EXPECT_TRUE(fs.write_charging("/f" + std::to_string(i), 0,
                                       bytes(std::string(1, 'a' + i)))
                        .has_value());
    }
    EXPECT_EQ(static_cast<std::size_t>(4), fs.bytes_used());
    EXPECT_EQ(static_cast<std::size_t>(4), fs.size());

    // Open /big (only the open registers it; bytes_used still 4).
    // Writing 2 bytes to /big delta=2; 4+2=6 > 4, evict until
    // 4-2freed + 2 <= 4. Need bytes_used <= 2, so evict two oldest.
    EXPECT_TRUE(fs.open("/big", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/big", 0, bytes("xy")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(4), fs.bytes_used());
    EXPECT_EQ(static_cast<std::size_t>(3), fs.size());
    EXPECT_TRUE(fs.lookup("/big").has_value());
    EXPECT_FALSE(fs.lookup("/f0").has_value());
    EXPECT_FALSE(fs.lookup("/f1").has_value());
}

TEST(ramfs, evict_returns_out_of_memory_when_too_big) {
    // Capacity = 4 bytes; single file needs to be 10 bytes.
    // After evicting everything, reservation still fails.
    RamFS fs(/*byte_capacity=*/4);
    EXPECT_TRUE(fs.open("/small", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/small", 0, bytes("xy")).has_value());

    EXPECT_TRUE(fs.open("/big", OpenFlags::Create).has_value());
    auto w = fs.write_charging("/big", 0, bytes(std::string(10, 'Z')));
    EXPECT_FALSE(w.has_value());
    EXPECT_EQ(ErrorKind::OutOfMemory, w.error().kind);
    // The smaller file is also evicted (we eagerly reclaim to fit,
    // but the request itself doesn't fit).
    EXPECT_EQ(static_cast<std::size_t>(0), fs.bytes_used());
}

TEST(ramfs, eviction_respects_recent_access) {
    // Capacity = 16 bytes; fit /a (5), /b (5), /c (3) = 13 bytes.
    // Touch /a so it has the latest tick.
    RamFS fs(/*byte_capacity=*/16);
    EXPECT_TRUE(fs.open("/a", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/a", 0, bytes("12345")).has_value());

    EXPECT_TRUE(fs.open("/b", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/b", 0, bytes("vwxyz")).has_value());

    // Touch /a — its last_access_ becomes the highest of the two.
    EXPECT_TRUE(fs.lookup("/a").has_value());

    EXPECT_TRUE(fs.open("/c", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/c", 0, bytes("XYZ")).has_value());
    EXPECT_TRUE(fs.lookup("/a").has_value());
    EXPECT_TRUE(fs.lookup("/b").has_value());
    EXPECT_TRUE(fs.lookup("/c").has_value());
    EXPECT_EQ(static_cast<std::size_t>(13), fs.bytes_used());
}

TEST(ramfs, eviction_picks_oldest_when_overflow_forces_eviction) {
    // Capacity = 5 bytes; populate /a (1) and /b (1) = 2 bytes used.
    // Then touch /b and write a 4-byte /c — forces eviction of /a
    // (1 byte freed), then 1+4=5 ≤ 5, OK.
    RamFS fs(/*byte_capacity=*/5);
    EXPECT_TRUE(fs.open("/a", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/a", 0, bytes("x")).has_value());
    EXPECT_TRUE(fs.open("/b", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/b", 0, bytes("y")).has_value());

    // Touch /b so its tick > /a's.
    EXPECT_TRUE(fs.lookup("/b").has_value());

    // Writing 4 bytes to /c: 2 + 4 = 6 > 5. Evict oldest = /a.
    EXPECT_TRUE(fs.open("/c", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/c", 0, bytes("ZZZZ")).has_value());

    EXPECT_FALSE(fs.lookup("/a").has_value());
    EXPECT_TRUE(fs.lookup("/b").has_value());
    EXPECT_TRUE(fs.lookup("/c").has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), fs.bytes_used());
}

TEST(ramfs, unbounded_capacity_never_evicts) {
    RamFS fs(/*byte_capacity=*/0);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(fs.open("/f" + std::to_string(i), OpenFlags::Create)
                        .has_value());
        EXPECT_TRUE(fs.write_charging("/f" + std::to_string(i), 0,
                                       bytes(std::string(100, 'x')))
                        .has_value());
    }
    EXPECT_EQ(static_cast<std::size_t>(20), fs.size());
    EXPECT_EQ(static_cast<std::size_t>(2000), fs.bytes_used());
}

// ---- Misc -------------------------------------------------------------

TEST(ramfs, open_existing_exclusive_errors) {
    RamFS fs(/*byte_capacity=*/0);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());
    auto second = fs.open("/x",
                           OpenFlags::Create | OpenFlags::Exclusive);
    EXPECT_FALSE(second.has_value());
    EXPECT_EQ(ErrorKind::InvalidArgument, second.error().kind);
}

TEST(ramfs, open_existing_truncate_zeroes_bytes_used) {
    RamFS fs(/*byte_capacity=*/0);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("0123456789")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(10), fs.bytes_used());

    auto fh = fs.open("/x", OpenFlags::Write | OpenFlags::Truncate);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), fs.bytes_used());

    auto stat = fs.lookup("/x").value()->stat();
    EXPECT_TRUE(stat.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0), stat->size);
}

TEST(ramfs, write_charging_missing_file_errors) {
    RamFS fs(/*byte_capacity=*/0);
    auto w = fs.write_charging("/nope", 0, bytes("x"));
    EXPECT_FALSE(w.has_value());
    EXPECT_EQ(ErrorKind::NotFound, w.error().kind);
}

TEST(ramfs, clear_resets_state) {
    RamFS fs(/*byte_capacity=*/16);
    EXPECT_TRUE(fs.open("/x", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.write_charging("/x", 0, bytes("0123456789")).has_value());
    EXPECT_EQ(static_cast<std::size_t>(10), fs.bytes_used());

    fs.clear();
    EXPECT_EQ(static_cast<std::size_t>(0), fs.size());
    EXPECT_EQ(static_cast<std::size_t>(0), fs.bytes_used());
    EXPECT_FALSE(fs.lookup("/x").has_value());
}

RUN_ALL_TESTS()