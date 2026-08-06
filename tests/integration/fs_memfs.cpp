// tests/integration/fs_memfs.cpp
//
// Open / write / read across MemFS, plus an OverlayFS round-trip
// that verifies copy-up behaviour.
//
// Verifies:
//   1. Create + write + read round-trip on MemFS.
//   2. truncate() shrinks the visible size.
//   3. Open without Create on a missing path errors with NotFound.
//   4. Open with Create on a missing path succeeds and produces an
//      empty file.
//   5. OverlayFS reads from the lower layer when the upper is
//      empty.
//   6. OverlayFS writes copy-up to the upper layer and the lower
//      layer is unchanged.

#include <cstdio>
#include <string>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/fs/memfs.hpp"
#include "neuro/fs/overlayfs.hpp"
#include "neuro/fs/vfs.hpp"

using neuro::fs::FileHandle;
using neuro::fs::MemFS;
using neuro::fs::OpenFlags;
using neuro::fs::OverlayFS;

namespace {

std::vector<std::byte> bytes(std::string_view s) {
    std::vector<std::byte> out(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
    }
    return out;
}

std::string as_string(const std::vector<std::byte>& v) {
    return std::string(reinterpret_cast<const char*>(v.data()), v.size());
}

}  // namespace

TEST(fs, memfs_create_write_read_round_trip) {
    MemFS fs;
    auto fh = fs.open("/hello.txt",
                      OpenFlags::Write | OpenFlags::Create);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(fh->path, "/hello.txt");

    auto payload = bytes("Hello, NeuroFS!");
    auto w = fs.write_all("/hello.txt", payload);
    EXPECT_TRUE(w.has_value());

    auto got = fs.read_all("/hello.txt");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), payload.size());
    EXPECT_EQ(as_string(*got), std::string("Hello, NeuroFS!"));
}

TEST(fs, memfs_truncate_shrinks) {
    MemFS fs;
    fs.write_all("/hello.txt", bytes("Hello, NeuroFS!"));

    auto v = fs.lookup("/hello.txt");
    EXPECT_TRUE(v.has_value());
    auto r = (*v)->truncate(5);
    EXPECT_TRUE(r.has_value());

    auto got = fs.read_all("/hello.txt");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), 5u);
    EXPECT_EQ(as_string(*got), std::string("Hello"));
}

TEST(fs, memfs_open_missing_without_create_errors) {
    MemFS fs;
    auto fh = fs.open("/missing.txt", OpenFlags::Read);
    EXPECT_FALSE(fh.has_value());
    EXPECT_EQ(fh.error().kind, neuro::core::ErrorKind::NotFound);
}

TEST(fs, memfs_open_missing_with_create_succeeds) {
    MemFS fs;
    auto fh = fs.open("/fresh.txt",
                      OpenFlags::Write | OpenFlags::Create);
    EXPECT_TRUE(fh.has_value());

    auto got = fs.read_all("/fresh.txt");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), 0u);
    EXPECT_EQ(fs.size(), 1u);
}

TEST(fs, overlay_reads_from_lower) {
    // Lower layer has a file; upper is empty. Reads should fall
    // through to the lower layer.
    auto lower = std::make_unique<MemFS>();
    lower->write_all("/etc/hostname", bytes("node-01"));

    OverlayFS overlay(*lower);
    overlay.set_upper(std::make_unique<MemFS>());

    auto got = overlay.read_all("/etc/hostname");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(as_string(*got), std::string("node-01"));
}

TEST(fs, overlay_writes_copy_up_to_upper) {
    // Lower layer has a file; opening it for write should copy
    // up to the upper layer, the lower stays unchanged, and the
    // upper reflects the new contents.
    auto lower = std::make_unique<MemFS>();
    lower->write_all("/etc/hostname", bytes("node-01"));

    OverlayFS overlay(*lower);
    overlay.set_upper(std::make_unique<MemFS>());

    // Open for write triggers copy-up.
    auto fh = overlay.open("/etc/hostname",
                           OpenFlags::Write);
    EXPECT_TRUE(fh.has_value());

    auto w = overlay.write_all("/etc/hostname", bytes("node-99"));
    EXPECT_TRUE(w.has_value());

    // Upper now has the new value.
    auto upper_payload = overlay.read_all("/etc/hostname");
    EXPECT_TRUE(upper_payload.has_value());
    EXPECT_EQ(as_string(*upper_payload), std::string("node-99"));

    // Lower is unchanged.
    auto lower_payload = lower->read_all("/etc/hostname");
    EXPECT_TRUE(lower_payload.has_value());
    EXPECT_EQ(as_string(*lower_payload), std::string("node-01"));
}

RUN_ALL_TESTS()
