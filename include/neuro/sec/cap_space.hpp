// neuro/sec/cap_space.hpp
//
// Per-thread CapabilitySpace: a radix-trie keyed by 64-bit handle that
// stores Capabilities per §4.2.
//
// Layout:
//   - Each space is owned by a single thread (Phase C: single-writer
//     only; multi-thread access needs an external mutex).
//   - Handles are 64-bit, allocated by the space; 0 means "no handle".
//   - Lookups return a copy of the Capability; the kernel verifies
//     object_id + epoch against the object table before honouring it.
//   - Insert/remove mutate the trie; copies of capabilities pointing
//     into the space are not automatically invalidated. Revocation
//     via CapEpoch (cap_ops.hpp) handles global invalidation.
//
// Trie structure (radix 16, depth 16):
//   handles are split into 16 nibbles (low → high); each level has up
//   to 16 children plus a value slot. Empty nodes are eagerly recycled.
//
// This file is the foundation for granting, attenuating, and revoking
// capabilities in Phase C; the C++ template instantiation is exhaustive
// at template-depth 16 — the kernel version will switch to a dynamic
// depth and shared ownership.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "neuro/core/capability.hpp"

namespace neuro::sec {

using neuro::core::Capability;
using neuro::core::CapRight;

constexpr std::size_t kRadixBits = 4;
constexpr std::size_t kRadixSize  = 1 << kRadixBits;   // 16
constexpr std::size_t kMaxDepth   = 64 / kRadixBits;  // 16
constexpr std::uint64_t kInvalidHandle = 0;

class CapabilitySpace {
public:
    CapabilitySpace() {
        // A capability space is rooted at handle=0 (the empty handle).
        // Root node is allocated lazily on first insert.
    }

    CapabilitySpace(const CapabilitySpace&)            = delete;
    CapabilitySpace& operator=(const CapabilitySpace&) = delete;

    // Allocate a fresh handle and bind `cap` to it.
    // Returns the new handle, or kInvalidHandle if the space is full.
    [[nodiscard]] std::uint64_t insert(Capability cap) {
        const auto h = mint_handle();
        if (h == kInvalidHandle) return kInvalidHandle;
        if (!insert_at(h, cap)) {
            // shouldn't happen; collapse handle
            return kInvalidHandle;
        }
        return h;
    }

    // Look up a handle in this space. Returns nullopt if absent.
    [[nodiscard]] std::optional<Capability> lookup(std::uint64_t h) const {
        if (h == kInvalidHandle) return std::nullopt;
        const Node* n = root_.get();
        for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
            if (!n) return std::nullopt;
            const auto idx = nibble_at(h, depth);
            n = n->child(idx).get();
        }
        if (!n) return std::nullopt;
        return n->value();
    }

    // Remove a handle from this space. The Capability itself is returned
    // if the caller wants to pass it elsewhere (e.g. grant).
    [[nodiscard]] std::optional<Capability> erase(std::uint64_t h) {
        // Walk path; rebuild nodes with the target sub-tree cleared.
        if (h == kInvalidHandle) return std::nullopt;
        std::array<std::uint64_t, kMaxDepth> path{};
        std::array<std::size_t,  kMaxDepth> idxs{};
        Node* n = root_.get();
        for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
            if (!n) return std::nullopt;
            const auto idx = nibble_at(h, depth);
            path[depth] = h;
            idxs[depth] = idx;
            n = n->child(idx).get();
        }
        if (!n || !n->has_value()) return std::nullopt;
        auto removed = n->take_value();
        --size_;
        // Compact path: clear empty sub-trees from leaf upward.
        for (std::size_t depth = kMaxDepth; depth > 0; --depth) {
            Node* parent = node_at(path, idxs, depth - 1);
            if (!parent) break;
            const auto idx = idxs[depth - 1];
            if (parent->child(idx) && parent->child(idx)->empty()) {
                parent->set_child(idx, nullptr);
                --node_count_;
            }
        }
        return removed;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    // Test helper: total number of nodes currently allocated.
    [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }

private:
    class Node {
    public:
        Node() = default;

        [[nodiscard]] std::unique_ptr<Node>& child(std::size_t idx) {
            return children_[idx];
        }
        [[nodiscard]] const std::unique_ptr<Node>& child(std::size_t idx) const {
            return children_[idx];
        }
        void set_child(std::size_t idx, std::unique_ptr<Node> n) {
            children_[idx] = std::move(n);
        }

        [[nodiscard]] bool has_value() const noexcept { return has_value_; }
        [[nodiscard]] Capability value() const {
            return value_;
        }
        Capability take_value() {
            has_value_ = false;
            return value_;
        }
        void set_value(Capability c) {
            value_ = c;
            has_value_ = true;
        }

        [[nodiscard]] bool empty() const noexcept {
            if (has_value_) return false;
            for (const auto& c : children_) if (c) return false;
            return true;
        }

    private:
        std::array<std::unique_ptr<Node>, kRadixSize> children_{};
        Capability                                    value_{};
        bool                                          has_value_{false};
    };

    [[nodiscard]] static std::size_t nibble_at(std::uint64_t h,
                                               std::size_t depth) noexcept {
        // depth=0 is the least-significant nibble.
        return static_cast<std::size_t>((h >> (depth * kRadixBits)) & 0xF);
    }

    // Allocate a unique handle. Strategy: monotonic counter starting at 1,
    // skip values whose insertion would collide with a currently occupied
    // terminal node. The space is bounded by depth=16=64-bit namespace;
    // we collide on overflow and return kInvalidHandle.
    [[nodiscard]] std::uint64_t mint_handle() noexcept {
        for (std::uint64_t attempt = 0; attempt < UINT64_MAX; ++attempt) {
            const std::uint64_t h = next_handle_++;
            if (h == kInvalidHandle) continue;   // 0 is reserved
            if (!handle_in_use(h)) return h;
        }
        return kInvalidHandle;
    }

    [[nodiscard]] bool handle_in_use(std::uint64_t h) const noexcept {
        const Node* n = root_.get();
        for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
            if (!n) return false;
            const auto idx = nibble_at(h, depth);
            n = n->child(idx).get();
        }
        return n && n->has_value();
    }

    bool insert_at(std::uint64_t h, Capability cap) {
        if (!root_) root_ = std::make_unique<Node>();
        Node* n = root_.get();
        for (std::size_t depth = 0; depth < kMaxDepth; ++depth) {
            const auto idx = nibble_at(h, depth);
            if (!n->child(idx)) {
                n->set_child(idx, std::make_unique<Node>());
                ++node_count_;
            }
            n = n->child(idx).get();
        }
        if (n->has_value()) return false; // already occupied
        n->set_value(cap);
        ++size_;
        return true;
    }

    [[nodiscard]] Node* node_at(const std::array<std::uint64_t, kMaxDepth>& path,
                                const std::array<std::size_t,  kMaxDepth>& idxs,
                                std::size_t depth) const {
        if (depth == 0) return root_.get();
        Node* n = root_.get();
        for (std::size_t d = 0; d < depth; ++d) {
            (void)path;
            if (!n) return nullptr;
            n = n->child(idxs[d]).get();
        }
        return n;
    }

    std::unique_ptr<Node>          root_;
    std::size_t                    size_      {0};
    std::size_t                    node_count_{1};  // root_ is allocated lazily
    std::uint64_t                  next_handle_{1};
};

}  // namespace neuro::sec