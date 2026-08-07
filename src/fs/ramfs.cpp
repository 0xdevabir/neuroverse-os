// src/fs/ramfs.cpp
//
// RamFS implementation — see include/neuro/fs/ramfs.hpp for the
// contract.

#include "neuro/fs/ramfs.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace neuro::fs {

// ---- RamVNode -----------------------------------------------------------

core::Result<std::size_t> RamVNode::read(std::uint64_t        offset,
                                          std::span<std::byte> buf) {
    std::lock_guard lk(mu_);
    if (offset > data_.size()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::InvalidArgument, 0,
            "RamVNode::read: offset past end of file"));
    }
    if (offset == data_.size() || buf.empty()) {
        return static_cast<std::size_t>(0);
    }
    std::size_t avail = data_.size() - static_cast<std::size_t>(offset);
    std::size_t n     = std::min(buf.size(), avail);
    std::memcpy(buf.data(), data_.data() + offset, n);
    return n;
}

core::Result<std::size_t> RamVNode::write(std::uint64_t               offset,
                                           std::span<const std::byte> buf) {
    std::lock_guard lk(mu_);
    if (offset > data_.size()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::InvalidArgument, 0,
            "RamVNode::write: offset past end of file"));
    }
    if (buf.empty()) return static_cast<std::size_t>(0);

    std::size_t end = static_cast<std::size_t>(offset) + buf.size();
    if (end > data_.size()) data_.resize(end, std::byte{0});
    std::memcpy(data_.data() + offset, buf.data(), buf.size());
    return buf.size();
}

core::Result<core::Unit> RamVNode::truncate(std::uint64_t size) {
    std::lock_guard lk(mu_);
    if (size < data_.size()) data_.resize(static_cast<std::size_t>(size));
    return core::Unit{};
}

core::Result<Stat> RamVNode::stat() {
    std::lock_guard lk(mu_);
    Stat s{};
    s.size = data_.size();
    s.type = FileType::Regular;
    s.gen  = 1;
    return s;
}

std::size_t RamVNode::size_bytes() const noexcept {
    std::lock_guard lk(mu_);
    return data_.size();
}

std::uint64_t RamVNode::last_access() const noexcept {
    // last_access_ is only written under mu_, but we read it without
    // a lock here because it's atomic with respect to its writer and
    // a stale read is acceptable for eviction ordering.
    return last_access_;
}

void RamVNode::touch(std::uint64_t now) noexcept {
    std::lock_guard lk(mu_);
    last_access_ = now;
}

// ---- RamFS --------------------------------------------------------------

core::Result<VNode*> RamFS::lookup(std::string_view path) {
    std::lock_guard lk(mu_);
    auto it = nodes_.find(std::string{path});
    if (it == nodes_.end()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotFound, 0, "RamFS::lookup: not found"));
    }
    it->second->touch(tick_.fetch_add(1, std::memory_order_relaxed));
    return it->second.get();
}

core::Result<FileHandle> RamFS::open(std::string_view path, OpenFlags flags) {
    std::lock_guard lk(mu_);

    auto it = nodes_.find(std::string{path});
    if (it != nodes_.end()) {
        if (has(flags, OpenFlags::Exclusive)) {
            return std::unexpected(core::Error::make(
                core::ErrorKind::InvalidArgument, 0,
                "RamFS::open: Exclusive flag on existing file"));
        }
        if (has(flags, OpenFlags::Truncate)) {
            std::size_t before = it->second->size_bytes();
            auto t = it->second->truncate(0);
            if (!t) return std::unexpected(t.error());
            bytes_used_ -= std::min(bytes_used_, before);
        }
        it->second->touch(tick_.fetch_add(1, std::memory_order_relaxed));
        return FileHandle{std::string{path}, flags, it->second->handle()};
    }

    if (!has(flags, OpenFlags::Create)) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotFound, 0,
            "RamFS::open: missing path without Create"));
    }

    // Brand-new file: empty payload, fits trivially under any budget.
    auto id   = next_id();
    auto node = std::make_unique<RamVNode>(id, std::string{path});
    node->touch(tick_.fetch_add(1, std::memory_order_relaxed));
    auto* raw = node.get();
    nodes_.emplace(std::string{path}, std::move(node));
    return FileHandle{std::string{path}, flags, raw->handle()};
}

core::Result<std::size_t>
RamFS::write_charging(std::string_view path, std::uint64_t offset,
                       std::span<const std::byte> bytes) {
    std::lock_guard lk(mu_);

    auto it = nodes_.find(std::string{path});
    if (it == nodes_.end()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotFound, 0,
            "RamFS::write_charging: file not found"));
    }

    // Compute how many new bytes this write would add to the file
    // and charge against the budget. Shrinking or in-place writes
    // don't charge anything.
    auto& node  = *it->second;
    std::size_t before_size = node.size_bytes();
    std::size_t end_off     = static_cast<std::size_t>(offset) + bytes.size();
    std::size_t after_size  = std::max(before_size, end_off);
    std::size_t delta       = after_size - before_size;

    if (delta > 0 && !reserve_locked(delta)) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::OutOfMemory, 0,
            "RamFS::write_charging: capacity exhausted"));
    }

    auto w = node.write(offset, bytes);
    if (!w) {
        // Roll back the reservation if the write itself failed.
        bytes_used_ -= std::min(bytes_used_, delta);
        return std::unexpected(w.error());
    }
    bytes_used_ += delta;
    node.touch(tick_.fetch_add(1, std::memory_order_relaxed));
    return *w;
}

std::size_t RamFS::bytes_used() const {
    std::lock_guard lk(mu_);
    return bytes_used_;
}

std::size_t RamFS::size() const {
    std::lock_guard lk(mu_);
    return nodes_.size();
}

void RamFS::clear() {
    std::lock_guard lk(mu_);
    nodes_.clear();
    bytes_used_ = 0;
}

std::size_t RamFS::evict_oldest_locked() {
    if (nodes_.empty()) return 0;
    auto oldest = nodes_.begin();
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->second->last_access() < oldest->second->last_access()) {
            oldest = it;
        }
    }
    std::size_t freed = oldest->second->size_bytes();
    nodes_.erase(oldest);
    bytes_used_ -= std::min(bytes_used_, freed);
    return freed;
}

bool RamFS::reserve_locked(std::size_t n) {
    if (byte_capacity_ == 0) return true;  // unbounded
    while (bytes_used_ + n > byte_capacity_) {
        if (nodes_.empty()) return false;
        evict_oldest_locked();
    }
    return true;
}

}  // namespace neuro::fs