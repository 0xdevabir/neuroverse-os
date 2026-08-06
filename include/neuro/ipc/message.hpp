// neuro/ipc/message.hpp
//
// Typed IPC message envelope.
//
// Per README §4.1 (NeuroCore.ipc):
//   - Every IPC transfer is a typed message: the receiver knows the
//     shape of the payload from the tag alone.
//   - Capabilities travel by reference: a cap is represented as a
//     64-bit handle that the receiver can resolve via their
//     CapabilitySpace.
//   - Inline bytes carry small arguments; larger blobs go through
//     shared regions carved out by NeuroMem (out of scope here).
//
// On the host scaffold we keep the envelope self-contained and
// trivially-copyable (modulo std::vector / std::optional), so it can
// ride on top of `neuro::core::Endpoint` without changes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace neuro::ipc {

// Tag namespace. The 16-bit tag namespace identifies the protocol
// family (e.g. NeuroProc, NeuroFS); the 16-bit op identifies the
// specific call within that family.
struct Tag {
    std::uint16_t ns;
    std::uint16_t op;

    constexpr Tag() noexcept : ns(0), op(0) {}
    constexpr Tag(std::uint16_t ns_, std::uint16_t op_) noexcept
        : ns(ns_), op(op_) {}

    constexpr std::uint32_t pack() const noexcept {
        return (std::uint32_t(ns) << 16) | std::uint32_t(op);
    }

    friend constexpr bool operator==(Tag a, Tag b) noexcept {
        return a.pack() == b.pack();
    }
    friend constexpr bool operator!=(Tag a, Tag b) noexcept {
        return !(a == b);
    }
};

// Capability transfer — represented as a 64-bit handle into the
// sender's CapabilitySpace. The receiver must resolve it through
// whatever cross-domain cap transfer primitive the kernel provides.
// For the host scaffold we just carry the raw handle.
struct CapRef {
    std::uint64_t handle;
};

// Typed message envelope. Exactly one of payload / cap carries data
// at a time on the host scaffold; the receiver branches on `tag`.
// Larger systems would use a tagged union here.
struct Message {
    Tag                          tag;
    std::vector<std::byte>       payload;
    std::optional<CapRef>        cap;

    Message() = default;
    Message(Tag t) : tag(t) {}

    Message(Tag t, std::vector<std::byte> bytes)
        : tag(t), payload(std::move(bytes)) {}

    Message(Tag t, CapRef c)
        : tag(t), cap(c) {}

    // Convenience constructors for the common cases.
    static Message empty(Tag t) {
        Message m(t);
        return m;
    }

    static Message bytes(Tag t, std::vector<std::byte> b) {
        return Message(t, std::move(b));
    }

    static Message with_cap(Tag t, CapRef c) {
        return Message(t, c);
    }

    [[nodiscard]] bool carries_cap() const noexcept {
        return cap.has_value();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return payload.size();
    }
};

}  // namespace neuro::ipc