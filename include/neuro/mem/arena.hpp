// neuro/mem/arena.hpp
//
// Simple bump allocator over a contiguous region.
// Mirrors README §9.6. Reset/reset_to extensions arrive in Phase D.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

namespace neuro::mem {

// A simple bump arena over a large mmap'd region.
class Arena {
public:
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

    [[nodiscard]] std::size_t used() const noexcept {
        return static_cast<std::size_t>(
            reinterpret_cast<std::uintptr_t>(cursor_) -
            reinterpret_cast<std::uintptr_t>(base_));
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return size_; }

private:
    void*          base_   = nullptr;
    std::byte*     cursor_ = nullptr;
    std::byte*     end_    = nullptr;
    std::size_t    size_   = 0;
};

}  // namespace neuro::mem