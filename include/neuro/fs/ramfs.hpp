// neuro/fs/ramfs.hpp
//
// Fixed-capacity in-memory file system with automatic eviction.
//
// Per Z6.10, RamFS is a sibling of MemFS that tracks a byte budget.
// When the total bytes stored would exceed the configured capacity,
// the file with the oldest last-access stamp is evicted. This is the
// test back-end used by integration tests that need bounded memory
// pressure (e.g. cache policies, learn optimizers).
//
// Eviction is keyed on a per-VNode "last access" tick incremented on
// every read/write/truncate and bumped on creation. The tick is
// monotonically increasing; the VNode with the smallest tick is the
// oldest and is the first to be evicted.
//
// Semantics:
//   - byte_capacity == 0 means "unbounded" (no eviction).
//   - on open(Create), if the new file wouldn't fit, evict until it
//     would.
//   - on write, if the post-write size would exceed capacity even
//     after evicting everything else, the write fails with
//     core::ErrorKind::OutOfMemory.
//   - on truncate(0), the file counts as 0 bytes and is the next
//     eviction candidate.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "neuro/core/result.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/fs/vnode.hpp"

namespace neuro::fs {

class RamVNode : public VNode {
public:
    RamVNode(VNodeId id, std::string path)
        : VNode(id, /*gen=*/1), path_(std::move(path)) {}

    core::Result<std::size_t> read(std::uint64_t        offset,
                                   std::span<std::byte> buf) override;
    core::Result<std::size_t> write(std::uint64_t               offset,
                                    std::span<const std::byte> buf) override;
    core::Result<core::Unit> truncate(std::uint64_t size) override;
    core::Result<Stat>       stat() override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::size_t        size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t      last_access() const noexcept;

    void touch(std::uint64_t now) noexcept;

private:
    mutable std::mutex    mu_;
    std::string           path_;
    std::vector<std::byte> data_;
    std::uint64_t         last_access_ = 0;
};

class RamFS : public VFS {
public:
    explicit RamFS(std::size_t byte_capacity) noexcept
        : byte_capacity_(byte_capacity) {}

    core::Result<VNode*> lookup(std::string_view path) override;
    core::Result<FileHandle>
    open(std::string_view path, OpenFlags flags) override;

    // Total bytes currently stored across all files (does not include
    // any overhead from the VNode structs themselves).
    [[nodiscard]] std::size_t bytes_used() const;

    // Configured byte capacity. Zero means unbounded.
    [[nodiscard]] std::size_t capacity() const noexcept {
        return byte_capacity_;
    }

    // Number of live files.
    [[nodiscard]] std::size_t size() const;

    // Write `bytes` to `path` starting at `offset`, charging the
    // delta against the byte budget and evicting oldest files if
    // needed. This is the proper entry point — use it in tests
    // instead of write_all. Returns the number of bytes written.
    core::Result<std::size_t>
    write_charging(std::string_view path, std::uint64_t offset,
                   std::span<const std::byte> bytes);

    // Test hook: drop everything and reset the byte counter.
    void clear();

private:
    // Evict the oldest file, returning the bytes freed. If nothing
    // is evictable, returns 0.
    std::size_t evict_oldest_locked();

    // Reserve `n` bytes against the budget. Evicts oldest files
    // until the request fits. Returns false if even with every
    // file evicted there's no room (capacity < n).
    bool reserve_locked(std::size_t n);

    mutable std::mutex                                       mu_;
    std::unordered_map<std::string, std::unique_ptr<RamVNode>> nodes_;
    std::size_t                                              byte_capacity_ = 0;
    mutable std::size_t                                      bytes_used_   = 0;
    std::atomic<std::uint64_t>                               tick_{1};
};

}  // namespace neuro::fs
