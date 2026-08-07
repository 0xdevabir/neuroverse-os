// tests/unit/core/span_test.cpp
//
// Tests for neuro::core::Span<T> — a non-owning view over a
// contiguous range. Header-only; we cover the public surface:
// default ctor, pointer/size ctor, array ctor, std::array ctor,
// container ctor, data/size/empty, indexing, front/back,
// iterators, first/last/subspan.

#include "neuro/core/span.hpp"

#include <array>
#include <cstddef>
#include <numeric>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Span;

namespace {

// Helper to sum the elements of any integer range.
template <class R>
long sum(R&& r) {
    long s = 0;
    for (auto x : r) s += x;
    return s;
}

}  // namespace

// ---- 1. Default ctor --------------------------------------------------

TEST(span, default_ctor_is_empty) {
    Span<int> s;
    EXPECT_EQ(0u, s.size());
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(nullptr, s.data());
}

// ---- 2. Pointer/size ctor --------------------------------------------

TEST(span, pointer_size_ctor) {
    std::array<int, 4> a{1, 2, 3, 4};
    Span<int> s(a.data(), a.size());
    EXPECT_EQ(4u, s.size());
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(a.data(), s.data());
    EXPECT_EQ(1, s[0]);
    EXPECT_EQ(4, s[3]);
}

// ---- 3. C-array deduction --------------------------------------------

TEST(span, c_array_ctor) {
    int a[3] = {10, 20, 30};
    Span<int> s(a);
    EXPECT_EQ(3u, s.size());
    EXPECT_EQ(10, s.front());
    EXPECT_EQ(30, s.back());
}

// ---- 4. std::array ctor (mutable + const) ----------------------------

TEST(span, std_array_ctor) {
    std::array<int, 3> a{7, 8, 9};
    Span<int> s(a);
    EXPECT_EQ(3u, s.size());
    EXPECT_EQ(7, s.front());
}

TEST(span, const_std_array_ctor) {
    const std::array<int, 2> a{1, 2};
    Span<const int> s(a);
    EXPECT_EQ(2u, s.size());
    EXPECT_EQ(1, s[0]);
    EXPECT_EQ(2, s[1]);
}

// ---- 5. Container ctor -----------------------------------------------

TEST(span, vector_ctor) {
    std::vector<int> v{5, 6, 7, 8, 9};
    Span<int> s(v);
    EXPECT_EQ(5u, s.size());
    EXPECT_EQ(v.data(), s.data());
}

// ---- 6. Iteration ----------------------------------------------------

TEST(span, iterator_range) {
    std::vector<int> v{1, 2, 3};
    Span<int> s(v);
    EXPECT_EQ(3, std::distance(s.begin(), s.end()));
    EXPECT_EQ(6, sum(s));
}

// ---- 7. first / last / subspan ---------------------------------------

TEST(span, first_last_subspan) {
    std::array<int, 6> a{0, 1, 2, 3, 4, 5};
    Span<int> s(a);

    auto f = s.first(3);
    EXPECT_EQ(3u, f.size());
    EXPECT_EQ(0, f.front());
    EXPECT_EQ(2, f.back());

    auto l = s.last(2);
    EXPECT_EQ(2u, l.size());
    EXPECT_EQ(4, l.front());
    EXPECT_EQ(5, l.back());

    auto m = s.subspan(2, 2);
    EXPECT_EQ(2u, m.size());
    EXPECT_EQ(2, m[0]);
    EXPECT_EQ(3, m[1]);
}

TEST(span, subspan_to_end) {
    std::array<int, 4> a{10, 20, 30, 40};
    Span<int> s(a);
    auto tail = s.subspan(2, 2);
    EXPECT_EQ(2u, tail.size());
    EXPECT_EQ(30, tail[0]);
    EXPECT_EQ(40, tail[1]);
}

// ---- 8. Const correctness --------------------------------------------

TEST(span, const_view_readonly) {
    const std::array<int, 3> a{1, 2, 3};
    Span<const int> s(a);
    EXPECT_EQ(3u, s.size());
    static_assert(std::is_same_v<decltype(s)::element_type, const int>);
}

// ---- 9. Bytes / strings ---------------------------------------------

TEST(span, byte_view) {
    std::array<std::byte, 4> buf{std::byte{0xDE}, std::byte{0xAD},
                                  std::byte{0xBE}, std::byte{0xEF}};
    Span<std::byte> s(buf);
    EXPECT_EQ(4u, s.size());
    EXPECT_EQ(std::byte{0xDE}, s[0]);
    EXPECT_EQ(std::byte{0xEF}, s.back());
}

// ---- 10. Empty span iteration is safe --------------------------------

TEST(span, empty_span_iteration) {
    Span<int> s;
    int count = 0;
    for ([[maybe_unused]] int x : s) ++count;
    EXPECT_EQ(0, count);
    EXPECT_EQ(s.begin(), s.end());
}

RUN_ALL_TESTS()
