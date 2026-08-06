// neuro/core/object_table.hpp
//
// Kernel object registry: a handle->pointer map keyed by KObject id.
//
// Phase B ships a small lock-free-ish (single-writer/multi-reader via
// shared_mutex) probe table. Phase C replaces it with a per-thread
// radix-trie CapabilitySpace keyed by 64-bit handle.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "neuro/core/kobject.hpp"

namespace neuro::core {

// Non-owning slot. Owning semantics live in the kernel's slab allocator
// (Phase D); for now we store raw pointers and trust the caller.
//
// generation semantics: the slot tracks the *current* generation of the
// object. A capability minted when the object was at generation `g` is
// valid only if the slot's current generation is still `g` (i.e. the
// object hasn't been retired since). On retire, generation increments
// and any cap with the old generation is invalidated.
struct ObjectSlot {
    std::uint64_t id            = 0;
    std::uint64_t generation    = 0;
    KObjectKind   kind          = KObjectKind::Untyped;
    KObject*      ptr           = nullptr;
    bool          present       = false;

    [[nodiscard]] bool valid_for(std::uint64_t want_id,
                                 std::uint64_t want_gen) const noexcept {
        return present && id == want_id && generation == want_gen;
    }
};

template <std::size_t N>
class ObjectTable {
public:
    ObjectTable() = default;

    // Insert or replace. Returns false on collision with a different id.
    bool insert(KObject* obj) noexcept {
        if (!obj) return false;
        std::unique_lock lk(mu_);
        const auto idx = probe(obj->id());
        if (slots_[idx].present && slots_[idx].id != obj->id()) {
            return false;
        }
        slots_[idx] = ObjectSlot{obj->id(), obj->generation(),
                                 obj->kind(), obj, true};
        ++count_;
        return true;
    }

    // Re-read the object's current generation into the slot. Call after
    // retiring the object so that lookups for the OLD generation return
    // null (because generation moved forward).
    void refresh(std::uint64_t id) noexcept {
        std::unique_lock lk(mu_);
        const auto idx = probe(id);
        if (!slots_[idx].present || slots_[idx].id != id) return;
        slots_[idx].generation = slots_[idx].ptr->generation();
    }

    // Look up by id + generation. Returns nullptr if absent or stale.
    KObject* lookup(std::uint64_t id, std::uint64_t generation) const noexcept {
        std::shared_lock lk(mu_);
        const auto idx = probe(id);
        const auto& s = slots_[idx];
        if (!s.valid_for(id, generation)) return nullptr;
        return s.ptr;
    }

    // Look up by id only. Caller must not rely on freshness; used during
    // destruction when generations may already have rolled.
    KObject* lookup_any_generation(std::uint64_t id) const noexcept {
        std::shared_lock lk(mu_);
        const auto idx = probe(id);
        const auto& s = slots_[idx];
        if (!s.present || s.id != id) return nullptr;
        return s.ptr;
    }

    // Remove. Bumps the slot's stored generation to retire dangling refs.
    bool remove(std::uint64_t id) noexcept {
        std::unique_lock lk(mu_);
        const auto idx = probe(id);
        if (!slots_[idx].present || slots_[idx].id != id) return false;
        slots_[idx].present = false;
        slots_[idx].ptr = nullptr;
        --count_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::shared_lock lk(mu_);
        return count_;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

private:
    [[nodiscard]] std::size_t probe(std::uint64_t id) const noexcept {
        // Open-address linear probing on the low bits of id.
        return static_cast<std::size_t>(id) % N;
    }

    mutable std::shared_mutex  mu_;
    std::array<ObjectSlot, N>  slots_{};
    std::size_t                count_{0};
};

}  // namespace neuro::core