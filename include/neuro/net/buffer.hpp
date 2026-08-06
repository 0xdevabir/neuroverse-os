// neuro/net/buffer.hpp
//
// Network buffer type.
//
// Per README §4.6:
//   - The kernel net stack works on scatter/gather iovecs so a
//     single send syscall can drain several non-contiguous regions
//     without copying.
//   - For the host scaffold we wrap a std::vector<std::byte> as
//     a single contiguous buffer; the kernel implementation
//     replaces this with a real iovec chain.
//
// All read/write APIs in the net sockets take/return NetBuffer so
// the boundary stays stable across the host-to-kernel swap.

#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

namespace neuro::net {

class NetBuffer {
public:
    NetBuffer() = default;
    explicit NetBuffer(std::size_t n) : data_(n) {}
    NetBuffer(const std::byte* p, std::size_t n)
        : data_(p, p + n) {}

    // Convenience: build a buffer from a string literal / std::string.
    static NetBuffer from_string(const char* s) {
        return NetBuffer(reinterpret_cast<const std::byte*>(s),
                         std::strlen(s));
    }
    static NetBuffer from_string(const std::string& s) {
        return NetBuffer(reinterpret_cast<const std::byte*>(s.data()),
                         s.size());
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty()        const noexcept { return data_.empty(); }
    [[nodiscard]] std::byte*       data()       noexcept { return data_.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return data_.data(); }

    void resize(std::size_t n) { data_.resize(n); }
    void reserve(std::size_t n) { data_.reserve(n); }
    void clear()               { data_.clear(); }

    void append(const NetBuffer& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
    }

    void append(const std::byte* p, std::size_t n) {
        data_.insert(data_.end(), p, p + n);
    }

    [[nodiscard]] std::string as_string() const {
        return std::string(reinterpret_cast<const char*>(data_.data()),
                           data_.size());
    }

    friend bool operator==(const NetBuffer& a, const NetBuffer& b) noexcept {
        return a.data_ == b.data_;
    }
    friend bool operator!=(const NetBuffer& a, const NetBuffer& b) noexcept {
        return !(a == b);
    }

private:
    std::vector<std::byte> data_;
};

}  // namespace neuro::net
