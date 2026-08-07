// tests/integration/fs_cap.cpp
//
// Z7.3 — file-handle capability-gated read.
//
// Mints a capability for a "file object" and demonstrates the
// gating pattern: the VFS::open call would, in the kernel, take a
// capability for the file as its argument. The capability must
// have the Read or Write right for the call to succeed.
//
// On the host scaffold we model this with a thin wrapper: an
// open_with_cap() helper that checks CapRights::Read against a
// passed-in capability before delegating to MemFS.

#include "tests/test_framework.hpp"

#include "neuro/fs/memfs.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/sec/cap_ops.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using neuro::core::CapRight;
using neuro::fs::FileHandle;
using neuro::fs::MemFS;
using neuro::fs::OpenFlags;
using neuro::sec::CapabilitySpace;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;

namespace {

constexpr std::uint64_t FILE_OBJECT_ID = 0xF11EF11EULL;

std::vector<std::byte> bytes_helper(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

// Simulated kernel primitive: open a file only if the caller
// holds the right on the file's capability.
neuro::core::Result<FileHandle>
open_with_cap(MemFS& fs, CapabilitySpace& space, CapEpoch& epoch,
              std::uint64_t file_cap_handle, CapRight required,
              std::string_view path, OpenFlags flags) {
    auto cap = CapOps::resolve(space, epoch, file_cap_handle, required);
    if (!cap) {
        return std::unexpected(neuro::core::Error::make(
            neuro::core::ErrorKind::NotPermitted, 0,
            "open_with_cap: capability missing required right"));
    }
    return fs.open(path, flags);
}

}  // namespace

TEST(fs_cap, open_with_read_right_succeeds) {
    MemFS fs;
    EXPECT_TRUE(fs.write_all("/file", bytes_helper("payload")).has_value());

    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, FILE_OBJECT_ID,
                                CapRight::Read, 1);

    auto fh = open_with_cap(fs, space, epoch, handle, CapRight::Read,
                             "/file", OpenFlags::Read);
    EXPECT_TRUE(fh.has_value());
    auto data = fs.read_all("/file");
    EXPECT_TRUE(data.has_value());
    EXPECT_EQ(std::string("payload"),
              std::string(reinterpret_cast<const char*>(data->data()),
                          data->size()));
}

TEST(fs_cap, open_without_read_right_fails) {
    MemFS fs;
    EXPECT_TRUE(fs.write_all("/file", bytes_helper("payload")).has_value());

    CapabilitySpace space;
    CapEpoch        epoch;
    // Cap holds only Write, not Read.
    auto handle = CapOps::mint(space, epoch, FILE_OBJECT_ID,
                                CapRight::Write, 1);

    auto fh = open_with_cap(fs, space, epoch, handle, CapRight::Read,
                             "/file", OpenFlags::Read);
    EXPECT_FALSE(fh.has_value());
    EXPECT_EQ(neuro::core::ErrorKind::NotPermitted, fh.error().kind);
}

TEST(fs_cap, write_requires_write_right) {
    MemFS fs;

    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, FILE_OBJECT_ID,
                                CapRight::Read, 1);

    // Read succeeds.
    auto fh_r = open_with_cap(fs, space, epoch, handle, CapRight::Read,
                               "/missing", OpenFlags::Read);
    // The cap check passes; the underlying open fails because the
    // file doesn't exist and Create is not set.
    EXPECT_FALSE(fh_r.has_value());

    // Write requires Write right; cap has only Read → fails.
    auto fh_w = open_with_cap(fs, space, epoch, handle, CapRight::Write,
                               "/missing", OpenFlags::Create);
    EXPECT_FALSE(fh_w.has_value());
    EXPECT_EQ(neuro::core::ErrorKind::NotPermitted, fh_w.error().kind);
}

TEST(fs_cap, revoke_invalidates_file_cap) {
    MemFS fs;
    EXPECT_TRUE(fs.write_all("/file", bytes_helper("data")).has_value());

    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, FILE_OBJECT_ID,
                                CapRight::Read, 1);

    // Initially opens.
    EXPECT_TRUE(open_with_cap(fs, space, epoch, handle, CapRight::Read,
                               "/file", OpenFlags::Read).has_value());

    CapOps::revoke(space, epoch);

    // After revoke, the cap is invalid → open fails.
    auto fh = open_with_cap(fs, space, epoch, handle, CapRight::Read,
                             "/file", OpenFlags::Read);
    EXPECT_FALSE(fh.has_value());
    EXPECT_EQ(neuro::core::ErrorKind::NotPermitted, fh.error().kind);
}

RUN_ALL_TESTS()