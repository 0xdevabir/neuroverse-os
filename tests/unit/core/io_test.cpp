// tests/unit/core/io_test.cpp
//
// Tests for the NeuroCore IO primitives: MemoryRegion (large MMIO/DRAM
// windows) and IOPort (small PIO registers). Both are KObjects with
// capability-gated accessors.

#include "neuro/core/capability.hpp"
#include "neuro/core/io.hpp"
#include "neuro/core/kobject.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::core::IOPort;
using neuro::core::KObject;
using neuro::core::MemoryKind;
using neuro::core::MemoryRegion;

namespace {

Capability cap_for(const KObject& o, CapRight r) {
    return Capability::mint(o.id(), r, /*epoch=*/1, /*generation=*/1);
}

MemoryRegion::Config cfg(std::uint64_t base, std::size_t size,
                         MemoryKind k = MemoryKind::Uncached,
                         bool pf = false) {
    MemoryRegion::Config c{};
    c.phys_base = base;
    c.size      = size;
    c.kind      = k;
    c.prefetch  = pf;
    return c;
}

}  // namespace

// ---- MemoryRegion metadata -------------------------------------------

TEST(io, memory_region_metadata) {
    std::vector<std::byte> backing(16, std::byte{0});
    MemoryRegion mr(cfg(0xDEAD0000ULL, backing.size(), MemoryKind::Device, true),
                    backing.data());
    EXPECT_EQ(0xDEAD0000ULL, mr.phys_base());
    EXPECT_EQ(static_cast<std::size_t>(16), mr.size());
    EXPECT_EQ(MemoryKind::Device, mr.kind());
}

// ---- MemoryRegion byte round-trip ------------------------------------

TEST(io, memory_region_byte_round_trip) {
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion mr(cfg(0x1000, backing.size()), backing.data());

    auto cap = cap_for(mr, CapRight::Read | CapRight::Write);
    EXPECT_TRUE(mr.write_byte(0, std::uint8_t{0xAB}, cap));
    EXPECT_TRUE(mr.write_byte(7, std::uint8_t{0xCD}, cap));

    auto r0 = mr.read_byte(0, cap);
    auto r7 = mr.read_byte(7, cap);
    EXPECT_TRUE(r0.has_value());
    EXPECT_TRUE(r7.has_value());
    EXPECT_EQ(static_cast<std::uint8_t>(0xAB), r0.value());
    EXPECT_EQ(static_cast<std::uint8_t>(0xCD), r7.value());
}

// ---- MemoryRegion u32 round-trip -------------------------------------

TEST(io, memory_region_u32_round_trip) {
    std::vector<std::byte> backing(16, std::byte{0});
    MemoryRegion mr(cfg(0x2000, backing.size(), MemoryKind::WriteBack),
                    backing.data());

    auto cap = cap_for(mr, CapRight::Read | CapRight::Write);
    EXPECT_TRUE(mr.write_u32(0, 0x12345678u, cap));
    EXPECT_TRUE(mr.write_u32(4, 0xCAFEBABEu, cap));
    EXPECT_TRUE(mr.write_u32(8, 0xDEADBEEFu, cap));

    EXPECT_EQ(0x12345678u, mr.read_u32(0, cap).value());
    EXPECT_EQ(0xCAFEBABEu, mr.read_u32(4, cap).value());
    EXPECT_EQ(0xDEADBEEFu, mr.read_u32(8, cap).value());
}

// ---- MemoryRegion capability gating ----------------------------------

TEST(io, memory_region_read_requires_read_right) {
    std::vector<std::byte> backing(4, std::byte{0});
    MemoryRegion mr(cfg(0x3000, backing.size()), backing.data());

    auto wonly = cap_for(mr, CapRight::Write);
    EXPECT_FALSE(mr.read_byte(0, wonly).has_value());

    auto ronly = cap_for(mr, CapRight::Read);
    EXPECT_FALSE(mr.write_byte(0, std::uint8_t{0xFF}, ronly));
}

// ---- MemoryRegion foreign cap rejection ------------------------------

TEST(io, memory_region_rejects_foreign_cap) {
    std::vector<std::byte> a(8, std::byte{0});
    std::vector<std::byte> b(8, std::byte{0});
    MemoryRegion ra(cfg(0x4000, a.size()), a.data());
    MemoryRegion rb(cfg(0x5000, b.size()), b.data());

    auto wrong_cap = cap_for(rb, CapRight::Read | CapRight::Write);
    EXPECT_FALSE(ra.read_byte(0, wrong_cap).has_value());
    EXPECT_FALSE(ra.write_byte(0, std::uint8_t{0x42}, wrong_cap));

    EXPECT_TRUE(rb.write_byte(0, std::uint8_t{0x42}, wrong_cap));
}

// ---- MemoryRegion out-of-range rejection ------------------------------

TEST(io, memory_region_rejects_out_of_range) {
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion mr(cfg(0x6000, backing.size()), backing.data());

    auto cap = cap_for(mr, CapRight::Read | CapRight::Write);

    EXPECT_FALSE(mr.read_byte(8, cap).has_value());
    EXPECT_FALSE(mr.write_byte(8, std::uint8_t{0x1}, cap));

    // u32 straddling the boundary must fail (offset 5 + 4 = 9 > 8).
    EXPECT_FALSE(mr.read_u32(5, cap).has_value());
    EXPECT_FALSE(mr.write_u32(5, 0xFFFFFFFFu, cap));

    EXPECT_TRUE(mr.write_byte(7, std::uint8_t{0x77}, cap));
    EXPECT_TRUE(mr.read_byte(7, cap).has_value());
}

// ---- MemoryRegion size_t overflow regression -------------------------

TEST(io, memory_region_bounds_check_no_overflow) {
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion mr(cfg(0x7000, backing.size()), backing.data());

    auto cap = cap_for(mr, CapRight::Read | CapRight::Write);
    const std::size_t huge = static_cast<std::size_t>(-2);
    EXPECT_FALSE(mr.read_byte(huge, cap).has_value());
    EXPECT_FALSE(mr.write_byte(huge, std::uint8_t{0xAA}, cap));
}

// ---- MemoryRegion raw pointer ----------------------------------------

TEST(io, memory_region_raw_pointer) {
    std::array<std::byte, 4> backing{};
    MemoryRegion mr(cfg(0x8000, backing.size()), backing.data());
    EXPECT_EQ(backing.data(), mr.raw());
}

// ---- IOPort metadata --------------------------------------------------

TEST(io, ioport_metadata) {
    IOPort p(0x3F8, 1);
    EXPECT_EQ(static_cast<std::uint16_t>(0x3F8), p.port());
    EXPECT_EQ(static_cast<std::size_t>(1),       p.width());
}

TEST(io, ioport_default_width) {
    IOPort p(0x60);
    EXPECT_EQ(static_cast<std::size_t>(1), p.width());
}

// ---- IOPort read/write round-trip ------------------------------------

TEST(io, ioport_round_trip) {
    IOPort p(0x3F8, 4);
    auto cap = cap_for(p, CapRight::Read | CapRight::Write);

    EXPECT_TRUE(p.write(cap, 0xCAFEBABEu));
    auto v = p.read(cap);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(0xCAFEBABEu, v.value());
}

// ---- IOPort capability gating ----------------------------------------

TEST(io, ioport_read_requires_read_right) {
    IOPort p(0x40);
    auto wonly = cap_for(p, CapRight::Write);
    EXPECT_FALSE(p.read(wonly).has_value());
    EXPECT_TRUE(p.write(wonly, 0x1234u));
}

TEST(io, ioport_write_requires_write_right) {
    IOPort p(0x41);
    auto ronly = cap_for(p, CapRight::Read);
    EXPECT_FALSE(p.write(ronly, 0x5678u));

    auto v = p.read(ronly);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0), v.value());
}

// ---- IOPort foreign cap rejection ------------------------------------

TEST(io, ioport_rejects_foreign_cap) {
    IOPort a(0x100);
    IOPort b(0x200);
    auto bcap = cap_for(b, CapRight::Read | CapRight::Write);
    EXPECT_FALSE(a.read(bcap).has_value());
    EXPECT_FALSE(a.write(bcap, 0xABCDu));
}

// ---- MemoryRegion MemoryKind::PIO distinct access path -------------

TEST(io, memory_region_pio_kind_round_trip) {
    // Z4.7: a MemoryRegion of kind PIO behaves like a regular
    // capability-gated byte window but reports its kind distinctly so
    // the kernel / drivers can route the access via port-I/O
    // instructions instead of an MMIO load/store.
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion pio(cfg(0xCF8, backing.size(), MemoryKind::PIO),
                     backing.data());
    EXPECT_EQ(MemoryKind::PIO, pio.kind());

    auto cap = cap_for(pio, CapRight::Read | CapRight::Write);
    EXPECT_TRUE(pio.write_byte(0, std::uint8_t{0xAB}, cap));
    EXPECT_TRUE(pio.write_byte(4, std::uint8_t{0xCD}, cap));

    EXPECT_EQ(static_cast<std::uint8_t>(0xAB), pio.read_byte(0, cap).value());
    EXPECT_EQ(static_cast<std::uint8_t>(0xCD), pio.read_byte(4, cap).value());

    // The capability gate still applies to PIO regions.
    auto ronly = cap_for(pio, CapRight::Read);
    EXPECT_FALSE(pio.write_byte(0, std::uint8_t{0xFF}, ronly));
}

TEST(io, memory_region_pio_distinct_from_device) {
    // PIO must be a distinct enum value from Device / Cached so that
    // routing decisions in the driver layer remain unambiguous.
    EXPECT_TRUE(MemoryKind::PIO != MemoryKind::Device);
    EXPECT_TRUE(MemoryKind::PIO != MemoryKind::Uncached);
    EXPECT_TRUE(MemoryKind::PIO != MemoryKind::Cached);
}

// ---- MemoryRegion prefetch hint is observable -----------------------

TEST(io, memory_region_prefetchable_default_false) {
    // Z4.8: prefetchable() defaults to false when Config::prefetch is
    // unset (the default-constructed Config has prefetch=false).
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion mr(cfg(0x9000, backing.size()), backing.data());
    EXPECT_FALSE(mr.prefetchable());
}

TEST(io, memory_region_prefetchable_true_when_set) {
    std::vector<std::byte> backing(8, std::byte{0});
    MemoryRegion mr(cfg(0xA000, backing.size(), MemoryKind::Device, true),
                    backing.data());
    EXPECT_TRUE(mr.prefetchable());
}

TEST(io, memory_region_prefetchable_independent_of_kind) {
    // Any MemoryKind can be marked prefetchable — the prefetch bit is
    // orthogonal to caching behaviour.
    for (auto k : {MemoryKind::Uncached, MemoryKind::Cached,
                   MemoryKind::WriteBack, MemoryKind::WriteCombine,
                   MemoryKind::Device,   MemoryKind::PIO}) {
        std::vector<std::byte> backing(4, std::byte{0});
        MemoryRegion mr(cfg(0xB000, backing.size(), k, true),
                        backing.data());
        EXPECT_TRUE(mr.prefetchable());
        EXPECT_EQ(k, mr.kind());
    }
}

RUN_ALL_TESTS()
