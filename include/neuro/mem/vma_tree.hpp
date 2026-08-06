// neuro/mem/vma_tree.hpp
//
// Virtual Memory Area (VMA) registry. The kernel and each userspace
// process track their address-space layout as a set of non-overlapping
// intervals keyed by [start, end).
//
// Phase D ships a balanced interval tree interface backed by a sorted
// std::vector. Lookups are O(log n) via binary search; inserts are
// O(n) due to shifts. A future commit replaces the backing with an
// augmented red-black tree (subtree max-endpoint) for true O(log n)
// inserts without changing the public API.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace neuro::mem {

struct VMA {
    std::uint64_t start;   // inclusive
    std::uint64_t end;     // exclusive
    std::uint32_t rights;  // capability rights for the mapping
    std::uint64_t backing; // host: arbitrary id; kernel: physical frame id

    [[nodiscard]] bool contains(std::uint64_t addr) const noexcept {
        return addr >= start && addr < end;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(end - start);
    }
};

class VMATree {
public:
    // Insert a VMA. Returns false if the interval overlaps an existing
    // VMA (caller must split / merge as appropriate).
    bool insert(const VMA& v) {
        if (v.start >= v.end) return false;
        for (const auto& e : entries_) {
            if (overlap(e, v)) return false;
        }
        entries_.push_back(v);
        std::sort(entries_.begin(), entries_.end(),
                  [](const VMA& a, const VMA& b) {
                      return a.start < b.start;
                  });
        return true;
    }

    // Erase the VMA whose start matches exactly.
    bool erase(std::uint64_t start) {
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), start,
            [](const VMA& v, std::uint64_t s) { return v.start < s; });
        if (it == entries_.end() || it->start != start) return false;
        entries_.erase(it);
        return true;
    }

    // Find the VMA containing `addr`. Returns nullopt if no match.
    [[nodiscard]] std::optional<VMA> find(std::uint64_t addr) const {
        // Binary-search the right edge first (largest start <= addr).
        auto it = std::upper_bound(
            entries_.begin(), entries_.end(), addr,
            [](std::uint64_t a, const VMA& v) { return a < v.start; });
        if (it == entries_.begin()) return std::nullopt;
        --it;
        if (it->contains(addr)) return *it;
        return std::nullopt;
    }

    // Iterate VMAs in address order. Caller must not mutate the tree
    // during iteration.
    [[nodiscard]] const std::vector<VMA>& intervals() const noexcept {
        return entries_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return entries_.empty(); }

    // Aggregate over all VMAs.
    [[nodiscard]] std::uint64_t total_size() const noexcept {
        std::uint64_t s = 0;
        for (const auto& v : entries_) s += v.size();
        return s;
    }

private:
    [[nodiscard]] static bool overlap(const VMA& a, const VMA& b) noexcept {
        return !(a.end <= b.start || b.end <= a.start);
    }

    std::vector<VMA> entries_;
};

}  // namespace neuro::mem