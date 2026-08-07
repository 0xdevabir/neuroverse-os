// tests/unit/fs/vfs_test.cpp
//
// Tests for neuro::fs::VFS — the abstract virtual file system.
//
// We don't link MemFS or OverlayFS here; instead we define a tiny
// in-memory "LinearVFS" that maps path -> MemVNode (defined inline
// for the test) and exercises the VFS contract directly.
//
// Coverage:
//   FileHandle:
//     - default ctor: empty path, Read flag, zero VNodeHandle
//   VFS::next_id():
//     - returns sequential distinct ids
//     - each id is greater than the previous
//   VFS::read_all / write_all via a minimal LinearVFS:
//     - write_all then read_all round-trips bytes
//     - read_all on empty file returns empty vector
//     - write_all overwrites prior contents
//     - read_all from unknown path returns an error
//   VFS::lookup via LinearVFS:
//     - returns the VNode backing the path after write_all
//     - returns error for unknown paths
//   VFS::open with OpenFlags::Create vs not:
//     - Create allows creation of new paths
//     - without Create, unknown paths error

#include "neuro/core/result.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/fs/vnode.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Result;
using neuro::core::Unit;
using neuro::fs::FileHandle;
using neuro::fs::FileType;
using neuro::fs::OpenFlags;
using neuro::fs::Stat;
using neuro::fs::VFS;
using neuro::fs::VNode;
using neuro::fs::VNodeHandle;
using neuro::fs::VNodeId;

namespace {

// Minimal in-memory VNode (same idea as in vnode_test.cpp).
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
        if (end > data_.size()) data_.resize(end, std::byte{0});
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

private:
    FileType                type_;
    std::vector<std::byte>  data_;
};

// Tiny in-memory VFS that maps path -> unique_ptr<VNode>.
// Implements only the methods we need for testing; MemFS would do
// this properly.
class LinearVFS final : public VFS {
public:
    Result<VNode*> lookup(std::string_view path) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = files_.find(std::string(path));
        if (it == files_.end()) {
            return std::unexpected(neuro::core::Error::make(
                neuro::core::ErrorKind::NotFound, 0, "no such file"));
        }
        return it->second.get();
    }

    Result<FileHandle> open(std::string_view path, OpenFlags flags) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::string p(path);
        auto it = files_.find(p);
        if (it == files_.end()) {
            if (!has(flags, OpenFlags::Create)) {
                return std::unexpected(neuro::core::Error::make(
                    neuro::core::ErrorKind::NotFound, 0, "no such file"));
            }
            auto vn = std::make_unique<MemVNode>(next_id());
            FileHandle fh{p, flags, vn->handle()};
            files_.emplace(p, std::move(vn));
            return fh;
        }
        if (has(flags, OpenFlags::Truncate)) {
            auto r = it->second->truncate(0);
            if (!r) return std::unexpected(r.error());
        }
        return FileHandle{p, flags, it->second->handle()};
    }

private:
    std::mutex                                       mu_;
    std::unordered_map<std::string,
                       std::unique_ptr<VNode>>      files_;
};

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

}  // namespace

// ---- 1. FileHandle -----------------------------------------------

TEST(vfs, filehandle_default) {
    FileHandle h{};
    EXPECT_TRUE(h.path.empty());
    EXPECT_EQ(OpenFlags::Read, h.flags);
    EXPECT_EQ(static_cast<VNodeId>(0), h.vnode.id);
    EXPECT_EQ(static_cast<neuro::fs::VNodeGen>(0), h.vnode.gen);
}

TEST(vfs, filehandle_field_assignment) {
    FileHandle h{};
    h.path  = "/etc/passwd";
    h.flags = OpenFlags::Read | OpenFlags::Write;
    h.vnode = VNodeHandle{42, 7};
    EXPECT_EQ(std::string("/etc/passwd"), h.path);
    EXPECT_TRUE(has(h.flags, OpenFlags::Read));
    EXPECT_TRUE(has(h.flags, OpenFlags::Write));
    EXPECT_FALSE(has(h.flags, OpenFlags::Append));
    EXPECT_EQ(static_cast<VNodeId>(42), h.vnode.id);
    EXPECT_EQ(static_cast<neuro::fs::VNodeGen>(7), h.vnode.gen);
}

// ---- 2. VFS::next_id ---------------------------------------------

TEST(vfs, next_id_is_monotonic) {
    LinearVFS fs;
    auto a = fs.next_id();
    auto b = fs.next_id();
    auto c = fs.next_id();
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b < c);
}

TEST(vfs, next_id_distinct) {
    LinearVFS fs;
    std::set<VNodeId> seen;
    for (int i = 0; i < 64; ++i) seen.insert(fs.next_id());
    EXPECT_EQ(static_cast<std::size_t>(64), seen.size());
}

TEST(vfs, next_id_starts_at_or_above_one) {
    LinearVFS fs;
    auto a = fs.next_id();
    EXPECT_TRUE(a > static_cast<VNodeId>(0));
}

// ---- 3. write_all / read_all round-trip -------------------------

TEST(vfs, write_all_then_read_all_round_trip) {
    LinearVFS fs;
    auto w = fs.write_all("/hello.txt", to_bytes("hello, world"));
    EXPECT_TRUE(w.has_value());

    auto r = fs.read_all("/hello.txt");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(12), r->size());
    std::string s(reinterpret_cast<const char*>(r->data()), r->size());
    EXPECT_EQ(std::string("hello, world"), s);
}

TEST(vfs, read_all_empty_file) {
    LinearVFS fs;
    fs.write_all("/empty", {});
    auto r = fs.read_all("/empty");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

TEST(vfs, write_all_overwrites_previous) {
    LinearVFS fs;
    fs.write_all("/x", to_bytes("first content"));
    fs.write_all("/x", to_bytes("second"));
    auto r = fs.read_all("/x");
    EXPECT_TRUE(r.has_value());
    std::string s(reinterpret_cast<const char*>(r->data()), r->size());
    EXPECT_EQ(std::string("second"), s);
}

TEST(vfs, read_all_unknown_path_errors) {
    LinearVFS fs;
    auto r = fs.read_all("/missing");
    EXPECT_FALSE(r.has_value());
}

TEST(vfs, write_all_then_read_all_binary) {
    LinearVFS fs;
    const std::uint8_t bytes[] = {0x00, 0xFF, 0x7F, 0x80, 0x42};
    std::vector<std::byte> payload(
        reinterpret_cast<const std::byte*>(bytes),
        reinterpret_cast<const std::byte*>(bytes) + 5);
    fs.write_all("/blob", payload);

    auto r = fs.read_all("/blob");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), r->size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x00),
              static_cast<std::uint8_t>((*r)[0]));
    EXPECT_EQ(static_cast<std::uint8_t>(0xFF),
              static_cast<std::uint8_t>((*r)[1]));
    EXPECT_EQ(static_cast<std::uint8_t>(0x42),
              static_cast<std::uint8_t>((*r)[4]));
}

// ---- 4. lookup --------------------------------------------------

TEST(vfs, lookup_returns_vnode_after_write_all) {
    LinearVFS fs;
    fs.write_all("/data", to_bytes("payload"));

    auto v = fs.lookup("/data");
    EXPECT_TRUE(v.has_value());
    EXPECT_NE(nullptr, *v);
    EXPECT_TRUE((*v)->handle().id != 0);

    auto s = (*v)->stat();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(7), s->size);
    EXPECT_EQ(FileType::Regular, s->type);
}

TEST(vfs, lookup_unknown_returns_error) {
    LinearVFS fs;
    auto v = fs.lookup("/nope");
    EXPECT_FALSE(v.has_value());
}

TEST(vfs, lookup_distinct_paths_distinct_vnodes) {
    LinearVFS fs;
    fs.write_all("/a", to_bytes("A"));
    fs.write_all("/b", to_bytes("B"));

    auto a = fs.lookup("/a");
    auto b = fs.lookup("/b");
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(b.has_value());
    auto id_a = (*a)->handle().id;
    auto id_b = (*b)->handle().id;
    EXPECT_TRUE(id_a != id_b);
}

// ---- 5. open with Create / no Create ----------------------------

TEST(vfs, open_without_create_errors_on_missing) {
    LinearVFS fs;
    auto fh = fs.open("/brand-new", OpenFlags::Read);
    EXPECT_FALSE(fh.has_value());
}

TEST(vfs, open_with_create_succeeds) {
    LinearVFS fs;
    auto fh = fs.open("/created", OpenFlags::Create | OpenFlags::Write);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(std::string("/created"), fh->path);
    EXPECT_TRUE(has(fh->flags, OpenFlags::Create));
    EXPECT_TRUE(has(fh->flags, OpenFlags::Write));
}

TEST(vfs, open_existing_returns_same_vnode) {
    LinearVFS fs;
    fs.write_all("/p", to_bytes("hi"));

    auto before = fs.lookup("/p");
    EXPECT_TRUE(before.has_value());
    auto id_before = (*before)->handle().id;

    auto fh = fs.open("/p", OpenFlags::Read);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(id_before, fh->vnode.id);
}

TEST(vfs, open_with_truncate_clears_file) {
    LinearVFS fs;
    fs.write_all("/p", to_bytes("original content"));

    auto fh = fs.open("/p", OpenFlags::Write | OpenFlags::Truncate);
    EXPECT_TRUE(fh.has_value());

    auto r = fs.read_all("/p");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

// ---- 6. VFS is non-copyable -------------------------------------

TEST(vfs, vfs_non_copyable) {
    static_assert(!std::is_copy_constructible_v<VFS>);
    static_assert(!std::is_copy_assignable_v<VFS>);
}

RUN_ALL_TESTS()
