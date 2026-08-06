// neuro/fs/vnode.hpp
//
// VNode trait + handle + open flags.
//
// Per README §4.7 (NeuroFS):
//   - A VNode is the file-system analogue of an inode: an abstract
//     node that exposes open / read / write / close / stat. Every
//     concrete file system (MemFS, OverlayFS, the COW B-tree, the
//     real on-disk FS) implements the VNode trait.
//   - A VNodeHandle is a stable (id, generation) tuple. When the
//     kernel swaps the underlying object (e.g., during COW), the
//     generation increments so old caps are invalidated.
//   - OpenFlags is a bitfield so the kernel can encode read / write
//     / create / truncate / exclusive atomically.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "neuro/core/result.hpp"

namespace neuro::fs {

using VNodeId       = std::uint64_t;
using VNodeGen      = std::uint64_t;

struct VNodeHandle {
    VNodeId  id   = 0;
    VNodeGen gen  = 0;

    friend constexpr bool operator==(VNodeHandle a, VNodeHandle b) noexcept {
        return a.id == b.id && a.gen == b.gen;
    }
    friend constexpr bool operator!=(VNodeHandle a, VNodeHandle b) noexcept {
        return !(a == b);
    }
};

enum class OpenFlags : std::uint32_t {
    Read      = 1u << 0,
    Write     = 1u << 1,
    Create    = 1u << 2,  // create if absent
    Truncate  = 1u << 3,  // truncate to 0 on open
    Exclusive = 1u << 4,  // fail if Create and file already exists
    Append    = 1u << 5,  // every write goes to the end
};

[[nodiscard]] constexpr OpenFlags operator|(OpenFlags a, OpenFlags b) noexcept {
    return static_cast<OpenFlags>(static_cast<std::uint32_t>(a) |
                                  static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr OpenFlags operator&(OpenFlags a, OpenFlags b) noexcept {
    return static_cast<OpenFlags>(static_cast<std::uint32_t>(a) &
                                  static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool has(OpenFlags f, OpenFlags bit) noexcept {
    return (static_cast<std::uint32_t>(f) &
            static_cast<std::uint32_t>(bit)) != 0;
}

// File type as exposed by stat().
enum class FileType : std::uint8_t {
    Regular,
    Directory,
    Symlink,
};

// Stat view: a snapshot of the metadata a VNode hands out.
struct Stat {
    FileType           type    = FileType::Regular;
    std::uint64_t      size    = 0;
    VNodeGen           gen     = 0;
};

// Forward declaration for the VFS trait.
class VNode;

// VNode: the abstract base type for everything in the file-system
// tree. Each concrete FS (MemFS, OverlayFS, ...) derives from this
// and supplies its own data + lock strategy.
//
// The methods are intentionally minimal: open / read / write /
// close / stat. The kernel-side cap model sits on top.
class VNode {
public:
    virtual ~VNode() = default;

    // Read up to buf.size() bytes from offset. Returns the number
    // of bytes actually read (0 == EOF). Negative offsets or
    // invalid arguments surface as core::Result.
    virtual core::Result<std::size_t> read(std::uint64_t        offset,
                                           std::span<std::byte> buf) = 0;

    virtual core::Result<std::size_t> write(std::uint64_t               offset,
                                            std::span<const std::byte> buf) = 0;

    // Truncate / extend to size bytes.
    virtual core::Result<core::Unit> truncate(std::uint64_t size) = 0;

    virtual core::Result<Stat> stat() = 0;

    [[nodiscard]] VNodeHandle handle() const noexcept {
        return VNodeHandle{id_, gen_};
    }

protected:
    VNode(VNodeId id, VNodeGen gen = 1) noexcept : id_(id), gen_(gen) {}
    VNode(const VNode&)            = default;
    VNode& operator=(const VNode&) = default;

    // Bumped by derived classes when the underlying object is
    // replaced (e.g., during COW).
    VNodeId  id_;
    VNodeGen gen_;
};

}  // namespace neuro::fs