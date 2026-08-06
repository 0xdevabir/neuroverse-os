// neuro/core/span.hpp
//
// Lightweight non-owning view over a contiguous range of T. Backed by a
// raw pointer and a size. Used throughout the kernel to pass buffers
// across subsystem boundaries without copying or lifetime entanglement.

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

namespace neuro::core {

template <class T>
class Span {
public:
    using element_type     = T;
    using value_type       = std::remove_cv_t<T>;
    using size_type        = std::size_t;
    using difference_type  = std::ptrdiff_t;
    using pointer          = T*;
    using const_pointer    = const T*;
    using reference        = T&;
    using const_reference  = const T&;
    using iterator         = T*;
    using const_iterator   = const T*;

    constexpr Span() noexcept = default;

    constexpr Span(T* data, size_type size) noexcept
        : data_(data), size_(size) {}

    template <std::size_t N>
    constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    template <std::size_t N>
    constexpr Span(std::array<value_type, N>& arr) noexcept
        : data_(arr.data()), size_(N) {}

    template <std::size_t N>
    constexpr Span(const std::array<value_type, N>& arr) noexcept
        : data_(arr.data()), size_(N) {}

    // Constrain to avoid accidental std::vector<bool> proxy references.
    template <class Container,
              class = std::enable_if_t<
                  !std::is_same_v<std::remove_cvref_t<Container>, Span> &&
                  std::is_convertible_v<
                      decltype(std::declval<Container&>().data()),
                      pointer> &&
                  std::is_convertible_v<
                      decltype(std::declval<Container&>().size()),
                      size_type>>>
    constexpr Span(Container& c) noexcept(noexcept(c.data()) && noexcept(c.size()))
        : data_(c.data()), size_(c.size()) {}

    constexpr pointer data() const noexcept { return data_; }
    constexpr size_type size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr reference operator[](size_type i) const noexcept {
        return data_[i];
    }

    constexpr reference front() const noexcept { return data_[0]; }
    constexpr reference back()  const noexcept { return data_[size_ - 1]; }

    constexpr iterator begin()  const noexcept { return data_; }
    constexpr iterator end()    const noexcept { return data_ + size_; }
    constexpr const_iterator cbegin() const noexcept { return data_; }
    constexpr const_iterator cend()   const noexcept { return data_ + size_; }

    [[nodiscard]] Span<T> first(size_type n) const noexcept {
        return Span<T>{data_, n};
    }
    [[nodiscard]] Span<T> last(size_type n) const noexcept {
        return Span<T>{data_ + (size_ - n), n};
    }
    [[nodiscard]] Span<T> subspan(size_type offset,
                                  size_type count) const noexcept {
        return Span<T>{data_ + offset, count};
    }

private:
    pointer   data_ = nullptr;
    size_type size_ = 0;
};

// Deduction guide so that span(arr) works for C-style arrays.
template <class T, std::size_t N>
Span(T (&)[N]) -> Span<T>;

template <class T, std::size_t N>
Span(std::array<T, N>&) -> Span<T>;

template <class T, std::size_t N>
Span(const std::array<T, N>&) -> Span<const T>;

}  // namespace neuro::core