// tests/unit/fs/vnode_test.cpp
//
// Tests for neuro::fs::VNode — the abstract base class for every
// node in the file-system tree (regular files, directories,
// symlinks, devices, ...).
//
// Coverage:
//   VNodeHandle:
//     - default-constructed id == 0, gen == 0
//     - equality / inequality (id match, gen mismatch)
//   OpenFlags:
//     - bitfield values reachable
//     - operator| combines bits
//     - operator& extracts bits
//     - has() returns true only when the bit is set
//   FileType:
//     - the three enum values are distinct
//   Stat:
//     - default-constructed fields are well-defined
//   VNode base class (via a minimal in-memory MemVNode subclass):
//     - handle() round-trips the (id, gen) tuple supplied at ctor
//     - generation bump on retire (via subclass helper)
//     - read after write returns the same bytes
//     - partial read returns the truncated byte count
//     - EOF returns 0
//     - write past EOF extends the file
//     - truncate shrinks and grows
//     - stat() reflects current size and type
//     - virtual destructor dispatches correctly

#include "neuro/fs/vnode.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Result;
using neuro::core::Unit;
using neuro::fs::FileType;
using neuro::fs::OpenFlags;
using neuro::fs::Stat;
using neuro::fs::VNode;
using neuro::fs::VNodeGen;
using neuro::fs::VNodeHandle;
using neuro::fs::VNodeId;

namespace {

// Minimal in-memory VNode used by the tests below. Stores its data
// in a std::vector<std::byte> and supports the four abstract ops.
class MemVNode final : public VNode {
public:
    MemVNode(VNodeId id, FileType type = FileType::Regular)
        : VNode(id), type_(type) {}

    Result<std::size_t> read(std::uint64_t        offset,
                             std::span<std::byte> buf) override {
        if (buf.empty()) return std::size_t{0};
        if (offset >= data_.size()) return std::size_t{0};
        const std::size_t avail = data_.size() - static_cast<std::size_t>(offset);
        const std::size_t n     = std::min(avail, buf.size());
        std::memcpy(buf.data(),
                    data_.data() + static_cast<std::size_t>(offset),
                    n);
        return n;
    }

    Result<std::size_t> write(std::uint64_t               offset,
                              std::span<const std::byte> buf) override {
        if (offset > data_.size()) {
            data_.resize(static_cast<std::size_t>(offset), std::byte{0});
        }
        const std::size_t end =
            static_cast<std::size_t>(offset) + buf.size();
        if (end > data_.size()) {
            data_.resize(end, std::byte{0});
        }
        std::memcpy(data_.data() + static_cast<std::size_t>(offset),
                    buf.data(), buf.size());
        return buf.size();
    }

    Result<Unit> truncate(std::uint64_t size) override {
        data_.resize(static_cast<std::size_t>(size), std::byte{0});
        return Unit{};
    }

    Result<Stat> stat() override {
        Stat s{};
        s.type = type_;
        s.size = data_.size();
        s.gen  = gen_;
        return s;
    }

    // Test-only: bump generation to simulate COW replacement.
    void bump_generation_for_test() noexcept { ++gen_; }

    FileType file_type() const noexcept { return type_; }

private:
    FileType                  type_;
    std::vector<std::byte>    data_;
};

}  // namespace

// ---- 1. VNodeHandle -----------------------------------------------

TEST(vnode, handle_default_zero) {
    VNodeHandle h{};
    EXPECT_EQ(static_cast<VNodeId>(0),  h.id);
    EXPECT_EQ(static_cast<VNodeGen>(0), h.gen);
}

TEST(vnode, handle_equality) {
    VNodeHandle a{7, 3};
    VNodeHandle b{7, 3};
    VNodeHandle c{7, 4};
    VNodeHandle d{8, 3};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);   // gen mismatch
    EXPECT_TRUE(a != d);   // id  mismatch
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
}

// ---- 2. OpenFlags bitfield ops -----------------------------------

TEST(vnode, openflags_bitfield_values) {
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 0),
              static_cast<std::uint32_t>(OpenFlags::Read));
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 1),
              static_cast<std::uint32_t>(OpenFlags::Write));
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 2),
              static_cast<std::uint32_t>(OpenFlags::Create));
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 3),
              static_cast<std::uint32_t>(OpenFlags::Truncate));
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 4),
              static_cast<std::uint32_t>(OpenFlags::Exclusive));
    EXPECT_EQ(static_cast<std::uint32_t>(1u << 5),
              static_cast<std::uint32_t>(OpenFlags::Append));
}

TEST(vnode, openflags_or_combines_bits) {
    auto f = OpenFlags::Read | OpenFlags::Write;
    EXPECT_TRUE(has(f, OpenFlags::Read));
    EXPECT_TRUE(has(f, OpenFlags::Write));
    EXPECT_FALSE(has(f, OpenFlags::Append));
    EXPECT_EQ(static_cast<std::uint32_t>(OpenFlags::Read) |
              static_cast<std::uint32_t>(OpenFlags::Write),
              static_cast<std::uint32_t>(f));
}

TEST(vnode, openflags_and_extracts_bits) {
    auto f = OpenFlags::Read | OpenFlags::Write | OpenFlags::Append;
    auto sub = f & OpenFlags::Write;
    EXPECT_TRUE(has(sub, OpenFlags::Write));
    EXPECT_FALSE(has(sub, OpenFlags::Read));
    EXPECT_FALSE(has(sub, OpenFlags::Append));
}

TEST(vnode, openflags_has_predicate) {
    auto f = OpenFlags::Create | OpenFlags::Exclusive;
    EXPECT_TRUE(has(f, OpenFlags::Create));
    EXPECT_TRUE(has(f, OpenFlags::Exclusive));
    EXPECT_FALSE(has(f, OpenFlags::Read));
    EXPECT_FALSE(has(f, OpenFlags::Write));
    EXPECT_FALSE(has(f, OpenFlags::Truncate));
    EXPECT_FALSE(has(f, OpenFlags::Append));
}

TEST(vnode, openflags_empty_has_nothing) {
    auto f = static_cast<OpenFlags>(0);
    EXPECT_FALSE(has(f, OpenFlags::Read));
    EXPECT_FALSE(has(f, OpenFlags::Write));
    EXPECT_FALSE(has(f, OpenFlags::Create));
    EXPECT_FALSE(has(f, OpenFlags::Truncate));
    EXPECT_FALSE(has(f, OpenFlags::Exclusive));
    EXPECT_FALSE(has(f, OpenFlags::Append));
}

// ---- 3. FileType --------------------------------------------------

TEST(vnode, filetype_enum_distinct) {
    EXPECT_NE(FileType::Regular,   FileType::Directory);
    EXPECT_NE(FileType::Regular,   FileType::Symlink);
    EXPECT_NE(FileType::Directory, FileType::Symlink);
}

// ---- 4. Stat defaults ---------------------------------------------

TEST(vnode, stat_defaults) {
    Stat s{};
    EXPECT_EQ(FileType::Regular, s.type);
    EXPECT_EQ(static_cast<std::uint64_t>(0), s.size);
    EXPECT_EQ(static_cast<VNodeGen>(0),       s.gen);
}

// ---- 5. VNode base class behavior via MemVNode -------------------

TEST(vnode, base_handle_round_trips) {
    MemVNode n(42);
    auto h = n.handle();
    EXPECT_EQ(static_cast<VNodeId>(42), h.id);
    EXPECT_EQ(static_cast<VNodeGen>(1),  h.gen);  // default gen = 1
}

TEST(vnode, base_generation_bump) {
    MemVNode n(7);
    EXPECT_EQ(static_cast<VNodeGen>(1), n.handle().gen);
    n.bump_generation_for_test();
    EXPECT_EQ(static_cast<VNodeGen>(2), n.handle().gen);
    n.bump_generation_for_test();
    EXPECT_EQ(static_cast<VNodeGen>(3), n.handle().gen);
}

TEST(vnode, write_then_read_round_trip) {
    MemVNode n(1);
    const std::uint8_t src[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto wr = n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 4));
    EXPECT_TRUE(wr.has_value());
    EXPECT_EQ(static_cast<std::size_t>(4), *wr);

    std::uint8_t dst[4] = {0, 0, 0, 0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 4));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(4), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), dst[1]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xBE), dst[2]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xEF), dst[3]);
}

TEST(vnode, read_partial_returns_truncated_count) {
    MemVNode n(1);
    const std::uint8_t src[] = {1, 2, 3, 4, 5};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 5));

    std::uint8_t dst[3] = {0, 0, 0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 3));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(3), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(1), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(2), dst[1]);
    EXPECT_EQ(static_cast<std::uint8_t>(3), dst[2]);
}

TEST(vnode, read_with_offset) {
    MemVNode n(1);
    const std::uint8_t src[] = {10, 20, 30, 40, 50};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 5));

    std::uint8_t dst[2] = {0, 0};
    auto rd = n.read(2, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 2));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(2), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(30), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(40), dst[1]);
}

TEST(vnode, read_at_eof_returns_zero) {
    MemVNode n(1);
    std::uint8_t dst[1] = {0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 1));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), *rd);
}

TEST(vnode, read_past_eof_returns_zero) {
    MemVNode n(1);
    const std::uint8_t src[] = {0xAA};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 1));

    std::uint8_t dst[1] = {0};
    auto rd = n.read(10, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 1));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), *rd);
}

TEST(vnode, write_past_eof_extends_file) {
    MemVNode n(1);
    const std::uint8_t src[] = {0xCA, 0xFE};
    auto wr = n.write(10, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 2));
    EXPECT_TRUE(wr.has_value());
    EXPECT_EQ(static_cast<std::size_t>(2), *wr);

    // Bytes 0..9 are zero, bytes 10..11 are 0xCA, 0xFE.
    std::uint8_t dst[12] = {0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 12));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(12), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), dst[9]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xCA), dst[10]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xFE), dst[11]);
}

TEST(vnode, truncate_shrinks) {
    MemVNode n(1);
    const std::uint8_t src[] = {1, 2, 3, 4, 5, 6, 7, 8};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 8));

    auto t = n.truncate(3);
    EXPECT_TRUE(t.has_value());

    std::uint8_t dst[3] = {0, 0, 0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 3));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(3), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(1), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(3), dst[2]);

    // Reading past the new EOF returns 0.
    std::uint8_t tail[1] = {0};
    auto rd2 = n.read(3, std::span<std::byte>(
            reinterpret_cast<std::byte*>(tail), 1));
    EXPECT_TRUE(rd2.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), *rd2);
}

TEST(vnode, truncate_grows_with_zeros) {
    MemVNode n(1);
    const std::uint8_t src[] = {0x42};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 1));

    auto t = n.truncate(4);
    EXPECT_TRUE(t.has_value());

    std::uint8_t dst[4] = {0, 0, 0, 0};
    auto rd = n.read(0, std::span<std::byte>(
            reinterpret_cast<std::byte*>(dst), 4));
    EXPECT_TRUE(rd.has_value());
    EXPECT_EQ(static_cast<std::size_t>(4), *rd);
    EXPECT_EQ(static_cast<std::uint8_t>(0x42), dst[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), dst[1]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), dst[2]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), dst[3]);
}

TEST(vnode, stat_reflects_current_size_and_type) {
    MemVNode n(99, FileType::Symlink);
    auto s0 = n.stat();
    EXPECT_TRUE(s0.has_value());
    EXPECT_EQ(FileType::Symlink, s0->type);
    EXPECT_EQ(static_cast<std::uint64_t>(0), s0->size);

    const std::uint8_t src[] = {1, 2, 3};
    n.write(0, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(src), 3));
    auto s1 = n.stat();
    EXPECT_TRUE(s1.has_value());
    EXPECT_EQ(FileType::Symlink, s1->type);
    EXPECT_EQ(static_cast<std::uint64_t>(3), s1->size);

    n.truncate(10);
    auto s2 = n.stat();
    EXPECT_TRUE(s2.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(10), s2->size);
}

TEST(vnode, virtual_dtor_picks_derived) {
    auto* p = new MemVNode(123);
    EXPECT_EQ(static_cast<VNodeId>(123), p->handle().id);
    delete p;  // must dispatch to MemVNode dtor cleanly
}

TEST(vnode, owned_by_unique_ptr) {
    std::unique_ptr<VNode> p = std::make_unique<MemVNode>(5);
    EXPECT_EQ(static_cast<VNodeId>(5), p->handle().id);
    p.reset();
    EXPECT_TRUE(!p);
}

RUN_ALL_TESTS()
