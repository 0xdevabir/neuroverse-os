// tests/unit/fs/memfs_test.cpp
//
// Direct unit tests for MemVNode and MemFS. The integration test covers
// MemFS through OverlayFS; these tests isolate byte-level I/O, metadata,
// open-flag behavior, stable handles, and lifecycle hooks.

#include "neuro/fs/memfs.hpp"

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
using neuro::fs::MemFS;
using neuro::fs::MemVNode;
using neuro::fs::OpenFlags;
using neuro::fs::VNodeHandle;

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

std::string text(const std::vector<std::byte>& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

}  // namespace

// ---- MemVNode -----------------------------------------------------------

TEST(memfs, vnode_starts_empty) {
    MemVNode node(17, "/empty");
    EXPECT_EQ(std::string("/empty"), node.path());
    EXPECT_TRUE(node.snapshot().empty());

    auto stat = node.stat();
    EXPECT_TRUE(stat.has_value());
    EXPECT_EQ(FileType::Regular, stat->type);
    EXPECT_EQ(static_cast<std::uint64_t>(0), stat->size);
    EXPECT_EQ(static_cast<std::uint64_t>(1), stat->gen);
    EXPECT_EQ(static_cast<std::uint64_t>(17), node.handle().id);
}

TEST(memfs, vnode_write_and_read_at_offset) {
    MemVNode node(1, "/file");
    auto initial = bytes("abcdef");
    auto written = node.write(0, initial);
    EXPECT_TRUE(written.has_value());
    EXPECT_EQ(initial.size(), *written);

    std::array<std::byte, 3> out{};
    auto read = node.read(2, out);
    EXPECT_TRUE(read.has_value());
    EXPECT_EQ(static_cast<std::size_t>(3), *read);
    EXPECT_EQ(static_cast<std::uint8_t>('c'), static_cast<std::uint8_t>(out[0]));
    EXPECT_EQ(static_cast<std::uint8_t>('e'), static_cast<std::uint8_t>(out[2]));
}

TEST(memfs, vnode_read_at_eof_returns_zero) {
    MemVNode node(1, "/file");
    node.load(bytes("abc"));
    std::array<std::byte, 4> out{};
    auto read = node.read(3, out);
    EXPECT_TRUE(read.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), *read);
}

TEST(memfs, vnode_read_past_eof_errors) {
    MemVNode node(1, "/file");
    node.load(bytes("abc"));
    std::array<std::byte, 1> out{};
    auto read = node.read(4, out);
    EXPECT_FALSE(read.has_value());
    EXPECT_EQ(ErrorKind::InvalidArgument, read.error().kind);
}

TEST(memfs, vnode_sparse_write_zero_fills_gap) {
    MemVNode node(1, "/sparse");
    auto payload = bytes("xy");
    auto written = node.write(3, payload);
    EXPECT_TRUE(written.has_value());
    EXPECT_EQ(static_cast<std::size_t>(2), *written);

    auto snapshot = node.snapshot();
    EXPECT_EQ(static_cast<std::size_t>(5), snapshot.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(snapshot[0]));
    EXPECT_EQ(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(snapshot[2]));
    EXPECT_EQ(static_cast<std::uint8_t>('x'), static_cast<std::uint8_t>(snapshot[3]));
    EXPECT_EQ(static_cast<std::uint8_t>('y'), static_cast<std::uint8_t>(snapshot[4]));
}

TEST(memfs, vnode_truncate_shrinks_and_extends) {
    MemVNode node(1, "/file");
    node.load(bytes("abcdef"));

    auto shrink = node.truncate(3);
    EXPECT_TRUE(shrink.has_value());
    EXPECT_EQ(std::string("abc"), text(node.snapshot()));

    auto extend = node.truncate(6);
    EXPECT_TRUE(extend.has_value());
    auto snapshot = node.snapshot();
    EXPECT_EQ(static_cast<std::size_t>(6), snapshot.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(snapshot[5]));
}

TEST(memfs, vnode_load_replaces_payload) {
    MemVNode node(1, "/file");
    node.load(bytes("first"));
    node.load(bytes("second"));
    EXPECT_EQ(std::string("second"), text(node.snapshot()));
}

// ---- MemFS --------------------------------------------------------------

TEST(memfs, lookup_missing_reports_not_found) {
    MemFS fs;
    auto result = fs.lookup("/missing");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(ErrorKind::NotFound, result.error().kind);
}

TEST(memfs, create_mints_stable_handle) {
    MemFS fs;
    auto opened = fs.open("/one", OpenFlags::Create | OpenFlags::Write);
    EXPECT_TRUE(opened.has_value());
    EXPECT_EQ(std::string("/one"), opened->path);
    EXPECT_TRUE(neuro::fs::has(opened->flags, OpenFlags::Create));
    EXPECT_EQ(static_cast<std::uint64_t>(1), opened->vnode.id);
    EXPECT_EQ(static_cast<std::uint64_t>(1), opened->vnode.gen);

    auto node = fs.lookup("/one");
    EXPECT_TRUE(node.has_value());
    EXPECT_EQ(opened->vnode, (*node)->handle());

    auto reopened = fs.open("/one", OpenFlags::Read);
    EXPECT_TRUE(reopened.has_value());
    EXPECT_EQ(opened->vnode, reopened->vnode);
}

TEST(memfs, separate_files_receive_distinct_ids) {
    MemFS fs;
    auto first = fs.open("/first", OpenFlags::Create);
    auto second = fs.open("/second", OpenFlags::Create);
    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
    EXPECT_TRUE(first->vnode.id != second->vnode.id);
    EXPECT_EQ(static_cast<std::size_t>(2), fs.size());
}

TEST(memfs, exclusive_create_existing_errors) {
    MemFS fs;
    auto first = fs.open("/file", OpenFlags::Create);
    EXPECT_TRUE(first.has_value());

    auto second = fs.open("/file", OpenFlags::Create | OpenFlags::Exclusive);
    EXPECT_FALSE(second.has_value());
    EXPECT_EQ(ErrorKind::InvalidArgument, second.error().kind);
    EXPECT_EQ(static_cast<std::size_t>(1), fs.size());
}

TEST(memfs, exclusive_create_missing_succeeds) {
    MemFS fs;
    auto opened = fs.open("/new", OpenFlags::Create | OpenFlags::Exclusive);
    EXPECT_TRUE(opened.has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), fs.size());
}

TEST(memfs, truncate_flag_empties_existing_file) {
    MemFS fs;
    auto write = fs.write_all("/file", bytes("payload"));
    EXPECT_TRUE(write.has_value());

    auto opened = fs.open("/file", OpenFlags::Write | OpenFlags::Truncate);
    EXPECT_TRUE(opened.has_value());
    auto read = fs.read_all("/file");
    EXPECT_TRUE(read.has_value());
    EXPECT_TRUE(read->empty());
}

TEST(memfs, write_all_replaces_existing_contents) {
    MemFS fs;
    EXPECT_TRUE(fs.write_all("/file", bytes("long payload")).has_value());
    EXPECT_TRUE(fs.write_all("/file", bytes("short")).has_value());

    auto read = fs.read_all("/file");
    EXPECT_TRUE(read.has_value());
    EXPECT_EQ(std::string("short"), text(*read));
}

TEST(memfs, empty_path_is_a_valid_key) {
    MemFS fs;
    auto opened = fs.open("", OpenFlags::Create);
    EXPECT_TRUE(opened.has_value());
    EXPECT_EQ(std::string(""), opened->path);
    EXPECT_TRUE(fs.lookup("").has_value());
}

TEST(memfs, clear_drops_every_file) {
    MemFS fs;
    EXPECT_TRUE(fs.open("/a", OpenFlags::Create).has_value());
    EXPECT_TRUE(fs.open("/b", OpenFlags::Create).has_value());
    EXPECT_EQ(static_cast<std::size_t>(2), fs.size());

    fs.clear();
    EXPECT_EQ(static_cast<std::size_t>(0), fs.size());
    EXPECT_FALSE(fs.lookup("/a").has_value());
    EXPECT_FALSE(fs.lookup("/b").has_value());
}

TEST(memfs, clear_does_not_reuse_vnode_ids) {
    MemFS fs;
    auto first = fs.open("/first", OpenFlags::Create);
    EXPECT_TRUE(first.has_value());
    const VNodeHandle first_handle = first->vnode;

    fs.clear();
    auto second = fs.open("/second", OpenFlags::Create);
    EXPECT_TRUE(second.has_value());
    EXPECT_TRUE(second->vnode.id > first_handle.id);
}

RUN_ALL_TESTS()
