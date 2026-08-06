// neuro/core/kobject.hpp
//
// Root of every kernel-tracked object. Provides a stable 48-bit object_id
// minted by a global monotonic allocator. Concrete kernel objects
// (threads, endpoints, memory maps, IRQ sources, IO ports) derive from
// KObject and acquire an id at construction.
//
// The id is the `object_id` field embedded in a Capability (§4.2 / §9.2),
// so KObject lifetime is the kernel authority for what a capability refers
// to.

#pragma once

#include <atomic>
#include <cstdint>

namespace neuro::core {

// Tagged enum so the type system can distinguish kernel kinds at compile
// time and so we can grow the namespace without breaking ABI.
enum class KObjectKind : std::uint8_t {
    Untyped   = 0,
    Thread    = 1,
    Endpoint  = 2,
    MemoryMap = 3,
    IrqSource = 4,
    IoPort    = 5,
    VNode     = 6,
    Driver    = 7,
    // Reserve up to 255 for future subsystems.
};

class KObject {
public:
    KObject() = delete;
    KObject(const KObject&) = delete;
    KObject& operator=(const KObject&) = delete;

    // `k` is the kind tag; the id is minted from the global counter.
    explicit KObject(KObjectKind k) noexcept
        : kind_(k), id_(next_id()) {}

    KObject(KObjectKind k, std::uint64_t explicit_id) noexcept
        : kind_(k), id_(explicit_id) {}

    virtual ~KObject() = default;

    [[nodiscard]] KObjectKind kind()   const noexcept { return kind_; }
    [[nodiscard]] std::uint64_t id()   const noexcept { return id_; }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

    // Bumped when the object is destroyed and reborn. Revocation in
    // NeuroSec uses generation to invalidate stale capability references.
    [[nodiscard]] std::uint64_t retire() noexcept {
        return generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    // Static accessor for the global id counter. The first call after
    // process start returns 1; 0 is reserved as "no object".
    [[nodiscard]] static std::uint64_t next_id() noexcept {
        return next_id_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    KObjectKind      kind_;
    std::uint64_t    id_;
    std::atomic<std::uint64_t> generation_{0};

    // Static counter; lives at static-init order independence by being
    // a directly-defined atomic (no constructor side effects).
    static std::atomic<std::uint64_t> next_id_;
};

inline std::atomic<std::uint64_t> KObject::next_id_{1};

}  // namespace neuro::core