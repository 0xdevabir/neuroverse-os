// neuro/fs/vfs.hpp
//
// Virtual file system: namespace + dispatch layer.
//
// Per README §4.7, the VFS is the per-process handle to a tree of
// VNodes. Implementations can be MemFS, OverlayFS, the COW
// B-tree, or any third-party backend that satisfies the interface.
//
// The VFS hands out FileHandle values (path + generation) that
// the caller can use to read / write / close without holding the
// path lookup. The kernel-side cap model sits on top of this.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "neuro/core/result.hpp"
#include "neuro/fs/vnode.hpp"

namespace neuro::fs {

// A file handle is the public-facing VFS object: a path + the
// open flags + a stable handle to the underlying VNode.
struct FileHandle {
    std::string  path;
    OpenFlags    flags = OpenFlags::Read;
    VNodeHandle  vnode;
};

// Per-FS state lives in a VFS instance. Concrete backends (MemFS,
// OverlayFS) inherit from VFS and override the lookup / open paths.
class VFS {
public:
    VFS() = default;
    VFS(const VFS&)            = delete;
    VFS& operator=(const VFS&) = delete;
    virtual ~VFS()             = default;

    // Look up a path, returning the VNode backing it. The
    // returned VNode is owned by the VFS.
    virtual core::Result<VNode*>
    lookup(std::string_view path) = 0;

    // Open a path with the given flags. Unknown paths with
    // OpenFlags::Create produce a new VNode; everything else
    // requires an existing entry.
    virtual core::Result<FileHandle>
    open(std::string_view path, OpenFlags flags) = 0;

    // Convenience: open + read the whole file.
    core::Result<std::vector<std::byte>>
    read_all(std::string_view path) {
        auto v = lookup(path);
        if (!v) return std::unexpected(v.error());
        auto s = (*v)->stat();
        if (!s) return std::unexpected(s.error());
        std::vector<std::byte> out(static_cast<std::size_t>(s->size));
        std::size_t total = 0;
        while (total < out.size()) {
            std::span<std::byte> rest(out.data() + total, out.size() - total);
            auto r = (*v)->read(total, rest);
            if (!r) return std::unexpected(r.error());
            if (*r == 0) break;  // EOF
            total += *r;
        }
        out.resize(total);
        return out;
    }

    // Convenience: write a full file (truncates on open).
    core::Result<core::Unit>
    write_all(std::string_view path, std::span<const std::byte> bytes) {
        auto fh = open(path, OpenFlags::Write | OpenFlags::Create |
                                 OpenFlags::Truncate);
        if (!fh) return std::unexpected(fh.error());
        auto v = lookup(path);
        if (!v) return std::unexpected(v.error());
        auto r = (*v)->write(0, bytes);
        if (!r) return std::unexpected(r.error());
        return core::Unit{};
    }

    // Z6.7: append `bytes` to the end of the file at `path`. Creates
    // the file if it doesn't exist. Writes always land at the current
    // EOF — the offset is derived from stat()->size at call time.
    core::Result<core::Unit>
    append(std::string_view path, std::span<const std::byte> bytes) {
        auto fh = open(path, OpenFlags::Write | OpenFlags::Create);
        if (!fh) return std::unexpected(fh.error());
        auto v = lookup(path);
        if (!v) return std::unexpected(v.error());
        auto s = (*v)->stat();
        if (!s) return std::unexpected(s.error());
        auto r = (*v)->write(s->size, bytes);
        if (!r) return std::unexpected(r.error());
        return core::Unit{};
    }

    // Next available VNodeId. Used by concrete FSes to mint
    // fresh ids.
    [[nodiscard]] VNodeId next_id() noexcept {
        return next_id_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<VNodeId> next_id_{1};
};

}  // namespace neuro::fs
