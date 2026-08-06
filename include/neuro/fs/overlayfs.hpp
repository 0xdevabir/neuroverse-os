// neuro/fs/overlayfs.hpp
//
// OverlayFS: a layered VFS that mounts an "upper" (read-write)
// backend over a "lower" (read-only) backend.
//
// Per README §4.7, NeuroFS uses a copy-on-write overlay model
// for containers: the container's root FS is the lower layer,
// the container's writes land on the upper layer, and reads
// fall through from upper to lower.
//
// On the host scaffold we wire two VFSes together with a small
// policy class. The kernel implementation will replace the
// underlying VFSes with cap-gated handles but keep the same
// dispatch shape.

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "neuro/core/result.hpp"
#include "neuro/fs/vfs.hpp"
#include "neuro/fs/vnode.hpp"

namespace neuro::fs {

// OverlayVNode: a VNode whose data lives in one of the two
// layers. Reads consult the upper first, then fall through to
// the lower. Writes always go to the upper (COW semantics).
class OverlayVNode : public VNode {
public:
    OverlayVNode(VNodeId id, VNode* upper, VNode* lower) noexcept
        : VNode(id, /*gen=*/1), upper_(upper), lower_(lower) {}

    core::Result<std::size_t> read(std::uint64_t        offset,
                                   std::span<std::byte> buf) override;
    core::Result<std::size_t> write(std::uint64_t               offset,
                                    std::span<const std::byte> buf) override;
    core::Result<core::Unit> truncate(std::uint64_t size) override;
    core::Result<Stat>       stat() override;

private:
    VNode* upper_;   // may be null if not yet copied up
    VNode* lower_;
};

class OverlayFS : public VFS {
public:
    OverlayFS(VFS& lower) : lower_(&lower) {}

    // Take ownership of an upper layer (typically a MemFS).
    void set_upper(std::unique_ptr<VFS> upper) {
        std::lock_guard lk(mu_);
        upper_ = std::move(upper);
    }

    core::Result<VNode*> lookup(std::string_view path) override;
    core::Result<FileHandle>
    open(std::string_view path, OpenFlags flags) override;

private:
    // Returns the writable upper VNode for `path`, performing a
    // copy-up from the lower layer when the path exists only
    // there.
    core::Result<VNode*> copy_up(std::string_view path);

    VFS*                                     lower_ = nullptr;
    std::unique_ptr<VFS>                     upper_;
    mutable std::mutex                       mu_;
};

}  // namespace neuro::fs