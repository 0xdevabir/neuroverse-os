// neuro/mem/arena.hpp
//
// Bump allocator over a contiguous region. Phase D extends the §9.6
// starter with checkpoint save / restore, mark/sweep regions, and
// queried remaining capacity.
//
// Lifetime model:
//   - `reset()` releases everything; the next allocation starts at `base_`.
//   - `reset_to(checkpoint)` releases everything allocated after the
//     checkpoint and keeps the checkpoint valid.
//   - Checkpoints are cheap integer offsets; saving one does NOT count
//     against `used()` (only allocated bytes do).

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace neuro::mem {

class Arena {
public:
    // Opaque checkpoint type — callers get a token from `save()` and pass
    // it back to `reset_to()`.
    struct Checkpoint {
        std::uint64_t offset;
    };

    explicit Arena(std::size_t bytes) : size_(bytes) {
        base_ = ::operator new(bytes);
        cursor_ = static_cast<std::byte*>(base_);
        end_    = static_cast<std::byte*>(base_) + bytes;
    }

    ~Arena() { ::operator delete(base_); }

    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&)                 = delete;
    Arena& operator=(Arena&&)      = delete;

    [[nodiscard]] std::byte* allocate(std::size_t n, std::size_t align = 16) {
        auto cur = reinterpret_cast<std::uintptr_t>(cursor_);
        auto aligned = (cur + align - 1) & ~(align - 1);
        auto next = aligned + n;
        if (next > reinterpret_cast<std::uintptr_t>(end_)) {
            throw std::bad_alloc{};
        }
        cursor_ = reinterpret_cast<std::byte*>(next);
        return reinterpret_cast<std::byte*>(aligned);
    }

    // Templated convenience: returns a typed pointer.
    template <class T, class... Args>
    [[nodiscard]] T* make(Args&&... args) {
        // Allocate raw storage and construct in place.
        void* mem = allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    // Release everything. Equivalent to reset_to(save()) but cheaper.
    void reset() noexcept {
        cursor_ = static_cast<std::byte*>(base_);
    }

    // Save the current cursor as a checkpoint.
    [[nodiscard]] Checkpoint save() const noexcept {
        return Checkpoint{used()};
    }

    // Roll back to a previously-saved checkpoint. Anything allocated
    // after the checkpoint is logically released. The checkpoint itself
    // remains valid, allowing safe save/reset_to interleaving.
    void reset_to(Checkpoint cp) noexcept {
        auto* target = static_cast<std::byte*>(base_) + cp.offset;
        if (target < cursor_) cursor_ = target;
    }

    // Move the cursor forward by `n` without initialising the bytes.
    // Caller must guarantee the bytes form a valid object.
    [[nodiscard]] std::byte* reserve(std::size_t n, std::size_t align = 16) {
        return allocate(n, align);
    }

    [[nodiscard]] std::size_t used()      const noexcept {
        return static_cast<std::size_t>(
            reinterpret_cast<std::uintptr_t>(cursor_) -
            reinterpret_cast<std::uintptr_t>(base_));
    }
    [[nodiscard]] std::size_t capacity()  const noexcept { return size_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return capacity() - used();
    }
    [[nodiscard]] bool         empty()    const noexcept {
        return cursor_ == static_cast<std::byte*>(base_);
    }
    [[nodiscard]] std::byte*  base()      const noexcept {
        return static_cast<std::byte*>(base_);
    }

private:
    void*          base_   = nullptr;
    std::byte*     cursor_ = nullptr;
    std::byte*     end_    = nullptr;
    std::size_t    size_   = 0;
};

}  // namespace neuro::mem