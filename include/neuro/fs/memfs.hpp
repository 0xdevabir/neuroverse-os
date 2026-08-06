// neuro/fs/memfs.hpp
//
// In-memory FS backed by an unordered_map of path -> VNode.
//
// Per README §4.7, the host scaffold needs a working FS that
// can be exercised without touching the disk. The kernel
// implementation will replace this with the COW B-tree; the
// public VFS+VNode surface stays.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "neuro/core/result.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/fs/vnode.hpp"

namespace neuro::fs {

class MemVNode : public VNode {
public:
    MemVNode(VNodeId id, std::string path)
        : VNode(id, /*gen=*/1), path_(std::move(path)) {}

    core::Result<std::size_t> read(std::uint64_t        offset,
                                   std::span<std::byte> buf) override;
    core::Result<std::size_t> write(std::uint64_t               offset,
                                    std::span<const std::byte> buf) override;
    core::Result<core::Unit> truncate(std::uint64_t size) override;
    core::Result<Stat>       stat() override;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Test hook: read the entire payload as a fresh vector.
    [[nodiscard]] std::vector<std::byte> snapshot() const {
        std::lock_guard lk(mu_);
        return data_;
    }

    // Test hook: load with a payload (used to seed files).
    void load(std::vector<std::byte> bytes) {
        std::lock_guard lk(mu_);
        data_ = std::move(bytes);
    }

private:
    mutable std::mutex    mu_;
    std::string           path_;
    std::vector<std::byte> data_;
};

class MemFS : public VFS {
public:
    MemFS() = default;

    core::Result<VNode*> lookup(std::string_view path) override;
    core::Result<FileHandle>
    open(std::string_view path, OpenFlags flags) override;

    // Test hook: drop everything.
    void clear() {
        std::lock_guard lk(mu_);
        nodes_.clear();
    }

    // Test hook: number of files.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mu_);
        return nodes_.size();
    }

private:
    mutable std::mutex                                       mu_;
    std::unordered_map<std::string, std::unique_ptr<MemVNode>> nodes_;
};

}  // namespace neuro::fs
