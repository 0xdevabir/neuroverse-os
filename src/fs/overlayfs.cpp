// src/fs/overlayfs.cpp
//
// OverlayFS implementation.

#include "neuro/fs/overlayfs.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace neuro::fs {

// ---- OverlayVNode ------------------------------------------------------

core::Result<std::size_t> OverlayVNode::read(std::uint64_t        offset,
                                              std::span<std::byte> buf) {
    if (upper_) {
        // Upper read; if it returns 0 bytes we may still have data
        // in the lower layer (sparse-upper case), so fall through.
        auto r = upper_->read(offset, buf);
        if (!r) return r;
        if (*r == buf.size()) return *r;
        // Short read: try lower for the remainder.
        if (lower_) {
            std::size_t got = *r;
            if (got < buf.size()) {
                std::span<std::byte> rest(buf.data() + got,
                                          buf.size() - got);
                auto lr = lower_->read(offset + got, rest);
                if (!lr) return std::unexpected(lr.error());
                got += *lr;
            }
            return got;
        }
        return *r;
    }
    if (lower_) return lower_->read(offset, buf);
    return std::unexpected(core::Error::make(
        core::ErrorKind::NotFound, 0,
        "OverlayVNode::read: no upper or lower"));
}

core::Result<std::size_t> OverlayVNode::write(std::uint64_t               offset,
                                               std::span<const std::byte> buf) {
    if (!upper_) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotPermitted, 0,
            "OverlayVNode::write: upper layer not configured"));
    }
    return upper_->write(offset, buf);
}

core::Result<core::Unit> OverlayVNode::truncate(std::uint64_t size) {
    if (!upper_) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotPermitted, 0,
            "OverlayVNode::truncate: upper layer not configured"));
    }
    return upper_->truncate(size);
}

core::Result<Stat> OverlayVNode::stat() {
    if (upper_) return upper_->stat();
    if (lower_) return lower_->stat();
    return std::unexpected(core::Error::make(
        core::ErrorKind::NotFound, 0,
        "OverlayVNode::stat: no upper or lower"));
}

// ---- OverlayFS ---------------------------------------------------------

core::Result<VNode*> OverlayFS::lookup(std::string_view path) {
    if (upper_) {
        auto v = upper_->lookup(path);
        if (v) return *v;
    }
    if (lower_) {
        return lower_->lookup(path);
    }
    return std::unexpected(core::Error::make(
        core::ErrorKind::NotFound, 0,
        "OverlayFS::lookup: path not found in any layer"));
}

// Copy `path` from the lower layer to the upper layer so subsequent
// writes hit the upper layer. Returns the upper VNode (or the lower
// VNode if the upper layer has no copy yet and no copy can happen).
core::Result<VNode*> OverlayFS::copy_up(std::string_view path) {
    std::lock_guard lk(mu_);
    if (!upper_) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotPermitted, 0,
            "OverlayFS::copy_up: upper layer not configured"));
    }

    // If the upper already has it, return that.
    if (auto v = upper_->lookup(path); v) return *v;

    if (!lower_) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::NotFound, 0,
            "OverlayFS::copy_up: no lower layer"));
    }
    auto lv = lower_->lookup(path);
    if (!lv) return std::unexpected(lv.error());

    // Read the lower payload and seed the upper.
    auto st = (*lv)->stat();
    if (!st) return std::unexpected(st.error());
    std::vector<std::byte> buf(static_cast<std::size_t>(st->size));
    std::size_t off = 0;
    while (off < buf.size()) {
        std::span<std::byte> rest(buf.data() + off, buf.size() - off);
        auto r = (*lv)->read(off, rest);
        if (!r) return std::unexpected(r.error());
        if (*r == 0) break;
        off += *r;
    }
    buf.resize(off);

    auto up_fh = upper_->open(path, OpenFlags::Write | OpenFlags::Create |
                                       OpenFlags::Truncate);
    if (!up_fh) return std::unexpected(up_fh.error());
    auto up_v = upper_->lookup(path);
    if (!up_v) return std::unexpected(up_v.error());
    auto w = (*up_v)->write(0, buf);
    if (!w) return std::unexpected(w.error());
    return *up_v;
}

core::Result<FileHandle> OverlayFS::open(std::string_view path,
                                          OpenFlags        flags) {
    // Read-only lookup: prefer upper, fall through to lower.
    if (upper_) {
        if (auto v = upper_->lookup(path); v) {
            return FileHandle{std::string{path}, flags, (*v)->handle()};
        }
    }
    if (lower_) {
        if (auto v = lower_->lookup(path); v) {
            // If we plan to write, copy up first.
            if (has(flags, OpenFlags::Write) ||
                has(flags, OpenFlags::Create)) {
                auto cu = copy_up(path);
                if (!cu) return std::unexpected(cu.error());
                return FileHandle{std::string{path}, flags, (*cu)->handle()};
            }
            return FileHandle{std::string{path}, flags, (*v)->handle()};
        }
    }
    // No lower entry — if Create is set, create in the upper.
    if (has(flags, OpenFlags::Create) && upper_) {
        auto fh = upper_->open(path, flags);
        if (!fh) return std::unexpected(fh.error());
        return fh;
    }
    return std::unexpected(core::Error::make(
        core::ErrorKind::NotFound, 0,
        "OverlayFS::open: path not found"));
}

}  // namespace neuro::fs