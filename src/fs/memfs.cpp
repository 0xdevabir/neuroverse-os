// src/fs/memfs.cpp
//
// In-memory FS implementation.

#include "neuro/fs/memfs.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace neuro::fs {

// ---- MemVNode ----------------------------------------------------------

core::Result<std::size_t> MemVNode::read(std::uint64_t        offset,
                                          std::span<std::byte> buf) {
    std::lock_guard lk(mu_);
    if (offset > data_.size()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::InvalidArgument, /*code=*/0,
            "MemVNode::read: offset past EOF"));
    }
    const std::size_t avail = data_.size() - static_cast<std::size_t>(offset);
    const std::size_t n     = std::min(buf.size(), avail);
    std::memcpy(buf.data(), data_.data() + offset, n);
    return n;
}

core::Result<std::size_t> MemVNode::write(std::uint64_t               offset,
                                           std::span<const std::byte> buf) {
    std::lock_guard lk(mu_);
    if (offset > data_.size()) {
        // Sparse writes: extend with zeros up to offset.
        data_.resize(static_cast<std::size_t>(offset), std::byte{0});
    }
    const std::size_t end = static_cast<std::size_t>(offset) + buf.size();
    if (end > data_.size()) data_.resize(end);
    std::memcpy(data_.data() + offset, buf.data(), buf.size());
    return buf.size();
}

core::Result<core::Unit> MemVNode::truncate(std::uint64_t size) {
    std::lock_guard lk(mu_);
    data_.resize(static_cast<std::size_t>(size));
    return core::Unit{};
}

core::Result<Stat> MemVNode::stat() {
    std::lock_guard lk(mu_);
    Stat s;
    s.type = FileType::Regular;
    s.size = data_.size();
    s.gen  = gen_;
    return s;
}

// ---- MemFS -------------------------------------------------------------

core::Result<VNode*> MemFS::lookup(std::string_view path_view) {
    std::lock_guard lk(mu_);
    const std::string path{path_view};
    auto it = nodes_.find(path);
    if (it == nodes_.end()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotFound, /*code=*/0,
            "MemFS::lookup: path not found"));
    }
    return it->second.get();
}

core::Result<FileHandle> MemFS::open(std::string_view path_view,
                                     OpenFlags        flags) {
    std::lock_guard lk(mu_);
    const std::string path{path_view};
    auto it = nodes_.find(path);

    const bool exists = (it != nodes_.end());

    if (!exists) {
        if (!has(flags, OpenFlags::Create)) {
            return std::unexpected(core::Error::make(
                core::ErrorKind::NotFound, /*code=*/0,
                "MemFS::open: path not found and Create not set"));
        }
        if (has(flags, OpenFlags::Exclusive) && exists) {
            return std::unexpected(core::Error::make(
                core::ErrorKind::InvalidArgument, /*code=*/0,
                "MemFS::open: Exclusive and file already exists"));
        }
        // Create a fresh VNode for the path.
        VNodeId id = next_id();
        auto vnode = std::make_unique<MemVNode>(id, path);
        MemVNode* raw = vnode.get();
        if (has(flags, OpenFlags::Truncate)) {
            // New file is empty; nothing to do beyond the make_unique.
        }
        nodes_.emplace(path, std::move(vnode));
        return FileHandle{path, flags, raw->handle()};
    }

    // Path exists.
    if (has(flags, OpenFlags::Exclusive) && has(flags, OpenFlags::Create)) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::InvalidArgument, /*code=*/0,
            "MemFS::open: Exclusive and Create on existing file"));
    }
    if (has(flags, OpenFlags::Truncate)) {
        auto r = it->second->truncate(0);
        if (!r) return std::unexpected(r.error());
    }
    return FileHandle{path, flags, it->second->handle()};
}

}  // namespace neuro::fs
