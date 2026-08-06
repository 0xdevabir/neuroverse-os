// neuro/mem/pool.hpp
//
// Fixed-size block pool allocator.
//
// Each Pool<T, N> owns N pre-allocated T slots. Free slots are linked
// through an intrusive free list (each free block stores a pointer to
// the next free block in its first sizeof(void*) bytes). Allocation
// pops the head of the free list; deallocation pushes the freed block
// back on. Both are O(1).
//
// The pool is type-aware only insofar as it computes block size and
// alignment from T. Slots are returned as raw bytes; the caller is
// responsible for placement-new / explicit destructor calls.
//
// Pool is not thread-safe by default; concurrent access needs an
// external mutex or per-thread pools (Phase E).

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace neuro::mem {

template <class T, std::size_t N>
class Pool {
public:
    static_assert(N > 0, "Pool size must be > 0");

    Pool() {
        // Lay out N contiguous T-sized blocks; each becomes either an
        // active object or a free-list node.
        storage_ = ::operator new(N * sizeof(T));
        head_ = nullptr;
        for (std::size_t i = 0; i < N; ++i) {
            auto* block = reinterpret_cast<void**>(
                static_cast<std::byte*>(storage_) + i * sizeof(T));
            *block = head_;
            head_  = block;
        }
    }

    ~Pool() { ::operator delete(storage_); }

    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&)                 = delete;
    Pool& operator=(Pool&&)      = delete;

    // Allocate a raw T-sized block. Returns nullptr when the pool is
    // exhausted.
    [[nodiscard]] void* allocate() noexcept {
        if (!head_) return nullptr;
        void* blk = head_;
        head_ = *reinterpret_cast<void**>(head_);
        ++in_use_;
        if (in_use_ > high_water_) high_water_ = in_use_;
        return blk;
    }

    // Return a block to the free list. UB if `p` was not produced by
    // allocate() on this pool.
    void deallocate(void* p) noexcept {
        if (!p) return;
        auto* blk = static_cast<void*>(p);
        *reinterpret_cast<void**>(blk) = head_;
        head_ = blk;
        --in_use_;
    }

    // Allocator-style typed wrappers.
    template <class... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* mem = allocate();
        if (!mem) throw std::bad_alloc{};
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <class U>
    void destroy(U* p) noexcept {
        if (!p) return;
        if constexpr (!std::is_void_v<U>) {
            p->~U();
        }
        deallocate(p);
    }

    [[nodiscard]] std::size_t capacity()   const noexcept { return N; }
    [[nodiscard]] std::size_t in_use()     const noexcept { return in_use_; }
    [[nodiscard]] std::size_t high_water() const noexcept { return high_water_; }
    [[nodiscard]] std::size_t available()  const noexcept { return N - in_use_; }

private:
    void*       storage_ = nullptr;
    void*       head_    = nullptr;
    std::size_t in_use_{0};     // all blocks start free
    std::size_t high_water_{0};
};

}  // namespace neuro::mem