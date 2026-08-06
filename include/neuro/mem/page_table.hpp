// neuro/mem/page_table.hpp
//
// PageTable trait + RadixPageTable in-memory implementation.
//
// The trait models the canonical 4 KiB page granularity used by the
// kernel (§4.3). Future commits add huge/giant-page support and
// integrate with the real x86_64 / ARM64 MMU drivers.
//
// RadixPageTable is a host-side stub that holds virtual→physical
// mappings in a std::unordered_map. The kernel implementation will
// compose a 4-level radix tree whose leaves are real PMEs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace neuro::mem {

constexpr std::size_t   kPageBits      = 12;       // 4 KiB
constexpr std::uint64_t kPageSize      = 1ULL << kPageBits;
constexpr std::uint64_t kPageMask      = kPageSize - 1;
constexpr std::size_t   kHugePageBits  = 21;       // 2 MiB
constexpr std::uint64_t kHugePageSize  = 1ULL << kHugePageBits;
constexpr std::uint64_t kGiantPageBits = 30;       // 1 GiB
constexpr std::uint64_t kGiantPageSize = 1ULL << kGiantPageBits;

enum class PagePerm : std::uint8_t {
    None    = 0,
    Read    = 1 << 0,
    Write   = 1 << 1,
    Exec    = 1 << 2,
    User    = 1 << 3,
    Global  = 1 << 4,
};

inline PagePerm operator|(PagePerm a, PagePerm b) noexcept {
    return static_cast<PagePerm>(static_cast<std::uint8_t>(a) |
                                 static_cast<std::uint8_t>(b));
}
inline PagePerm operator&(PagePerm a, PagePerm b) noexcept {
    return static_cast<PagePerm>(static_cast<std::uint8_t>(a) &
                                 static_cast<std::uint8_t>(b));
}

struct PTE {
    std::uint64_t phys   : 52;     // physical page frame number
    PagePerm      perm   : 8;
    bool          present: 1;
    bool          dirty  : 1;
    bool          accessed : 1;
};

// Trait — every concrete page table satisfies this interface.
class PageTable {
public:
    virtual ~PageTable() = default;

    // Map `vaddr` (page-aligned) to the page frame `paddr` with `perm`.
    virtual bool map(std::uint64_t vaddr, std::uint64_t paddr,
                     PagePerm perm) = 0;

    // Unmap the page containing `vaddr`. Returns true if a mapping was
    // removed.
    virtual bool unmap(std::uint64_t vaddr) = 0;

    // Look up the PTE for `vaddr`.
    [[nodiscard]] virtual std::optional<PTE> lookup(std::uint64_t vaddr) const = 0;

    // Translate a virtual address to its physical address.
    [[nodiscard]] virtual std::optional<std::uint64_t>
    translate(std::uint64_t vaddr) const = 0;

    // Number of mapped pages.
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    // Flush any in-CPU cache for the page containing `vaddr`. No-op on
    // host; the kernel implementation issues INVLPG / TLBI.
    virtual void flush(std::uint64_t /*vaddr*/) noexcept {}

    // Convenience: round vaddr down to the page boundary.
    [[nodiscard]] static std::uint64_t page_base(std::uint64_t v) noexcept {
        return v & ~kPageMask;
    }
    [[nodiscard]] static std::uint64_t offset_in_page(std::uint64_t v) noexcept {
        return v & kPageMask;
    }
};

// RadixPageTable: stub implementation backed by a hash map. The kernel
// path replaces the body with a 4-level tree.
class RadixPageTable : public PageTable {
public:
    bool map(std::uint64_t vaddr, std::uint64_t paddr,
             PagePerm perm) override {
        const auto key = page_base(vaddr);
        entries_[key] = PTE{
            .phys    = paddr >> kPageBits,
            .perm    = perm,
            .present = true,
            .dirty   = false,
            .accessed = false,
        };
        return true;
    }

    bool unmap(std::uint64_t vaddr) override {
        return entries_.erase(page_base(vaddr)) > 0;
    }

    [[nodiscard]] std::optional<PTE>
    lookup(std::uint64_t vaddr) const override {
        auto it = entries_.find(page_base(vaddr));
        if (it == entries_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::optional<std::uint64_t>
    translate(std::uint64_t vaddr) const override {
        auto pte = lookup(vaddr);
        if (!pte || !pte->present) return std::nullopt;
        return (pte->phys << kPageBits) | offset_in_page(vaddr);
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        return entries_.size();
    }

private:
    std::unordered_map<std::uint64_t, PTE> entries_;
};

}  // namespace neuro::mem