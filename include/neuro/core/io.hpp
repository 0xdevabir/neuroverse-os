// neuro/core/io.hpp
//
// IOPort (PIO / small MMIO regions) and MemoryRegion (larger MMIO / DRAM
// windows). Both are KObjects with capability-gated accessors.
//
// Host scaffold: the backing memory is an in-process allocation. The
// kernel phase replaces the backing with real physical address mapping.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

#include "neuro/core/capability.hpp"
#include "neuro/core/kobject.hpp"

namespace neuro::core {

// ---- MemoryRegion ------------------------------------------------------

enum class MemoryKind : std::uint8_t {
    Uncached   = 0,
    Cached     = 1,
    WriteBack  = 2,
    WriteCombine = 3,
    Device     = 4,
    PIO        = 5,   // port-mapped I/O — distinct access path
};

class MemoryRegion : public KObject {
public:
    struct Config {
        std::uint64_t phys_base = 0;       // host: arbitrary unique id
        std::size_t   size      = 0;
        MemoryKind    kind      = MemoryKind::Uncached;
        bool          prefetch  = false;
    };

    // Host allocation constructor (no real physical address).
    MemoryRegion(Config cfg, std::byte* backing)
        : KObject(KObjectKind::IoPort), cfg_(cfg), backing_(backing) {}

    [[nodiscard]] std::uint64_t phys_base() const noexcept { return cfg_.phys_base; }
    [[nodiscard]] std::size_t   size()      const noexcept { return cfg_.size; }
    [[nodiscard]] MemoryKind    kind()      const noexcept { return cfg_.kind; }
    [[nodiscard]] bool          prefetchable() const noexcept {
        return cfg_.prefetch;
    }

    // Capability-gated byte access. Returns nullopt if `cap` lacks the
    // requested right or the offset is out of range.
    [[nodiscard]] std::optional<std::uint8_t>
    read_byte(std::size_t offset, Capability cap) const noexcept {
        if (!check(cap, CapRight::Read, offset, sizeof(std::uint8_t))) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(backing_[offset]);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    read_u32(std::size_t offset, Capability cap) const noexcept {
        if (!check(cap, CapRight::Read, offset, sizeof(std::uint32_t))) {
            return std::nullopt;
        }
        std::uint32_t v;
        std::memcpy(&v, backing_ + offset, sizeof(v));
        return v;
    }

    [[nodiscard]] bool
    write_byte(std::size_t offset, std::uint8_t value, Capability cap) noexcept {
        if (!check(cap, CapRight::Write, offset, sizeof(std::uint8_t))) {
            return false;
        }
        backing_[offset] = std::byte{value};
        return true;
    }

    [[nodiscard]] bool
    write_u32(std::size_t offset, std::uint32_t value, Capability cap) noexcept {
        if (!check(cap, CapRight::Write, offset, sizeof(std::uint32_t))) {
            return false;
        }
        std::memcpy(backing_ + offset, &value, sizeof(value));
        return true;
    }

    // Direct pointer for kernel-internal paths (bypasses capability).
    [[nodiscard]] std::byte* raw() const noexcept { return backing_; }

private:
    [[nodiscard]] bool check(Capability cap, CapRight r,
                             std::size_t offset, std::size_t width) const noexcept {
        return cap.verify() && cap.has(r)
            && cap.object_id == id()
            && offset + width <= cfg_.size;
    }

    Config                  cfg_;
    std::byte*              backing_;
};

// ---- IOPort ------------------------------------------------------------

// Small PIO/MMIO register window (typically < 256 bytes). Tracks the
// individual sub-register widths so the kernel can reject misaligned
// accesses.
class IOPort : public KObject {
public:
    struct Register {
        std::size_t   offset  = 0;
        std::size_t   width   = 1;     // bytes
        std::uint64_t reset   = 0;
    };

    IOPort(std::uint16_t port, std::size_t width = 1)
        : KObject(KObjectKind::IoPort), port_(port), width_(width) {}

    [[nodiscard]] std::uint16_t port()  const noexcept { return port_; }
    [[nodiscard]] std::size_t   width() const noexcept { return width_; }

    // Capability-gated read/write. In a real kernel these become
    // in/out instructions (x86) or MMIO loads (ARM/RISC-V).
    [[nodiscard]] std::optional<std::uint64_t>
    read(Capability cap) const noexcept {
        if (!cap_ok(cap, CapRight::Read)) return std::nullopt;
        return value_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool
    write(Capability cap, std::uint64_t v) noexcept {
        if (!cap_ok(cap, CapRight::Write)) return false;
        value_.store(v, std::memory_order_relaxed);
        return true;
    }

private:
    [[nodiscard]] bool cap_ok(Capability cap, CapRight r) const noexcept {
        return cap.verify() && cap.has(r) && cap.object_id == id();
    }

    std::uint16_t                       port_;
    std::size_t                         width_;
    std::atomic<std::uint64_t>          value_{0};
};

}  // namespace neuro::core