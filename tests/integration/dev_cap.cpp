// tests/integration/dev_cap.cpp
//
// Z7.6 — driver attaches a MemoryRegion whose access is gated by a
// capability.
//
// Demonstrates the standard driver pattern:
//   1. A driver advertises a region in regions().
//   2. The kernel / bus creates a cap for the region.
//   3. The driver holds the cap and uses it to read/write the region.
//   4. A cap without the right is rejected; a cap to a different
//      region is rejected; out-of-bounds writes are rejected.

#include "tests/test_framework.hpp"

#include "neuro/core/capability.hpp"
#include "neuro/core/io.hpp"
#include "neuro/core/kobject.hpp"
#include "neuro/dev/driver.hpp"

#include <array>
#include <cstdint>
#include <memory>

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::core::KObject;
using neuro::core::MemoryKind;
using neuro::core::MemoryRegion;

namespace {

constexpr std::uint64_t REGION_OBJECT_ID_BASE = 0xD0000000ULL;

// A minimal driver that advertises one MMIO region.
class MmioDriver : public neuro::dev::Driver {
public:
    MmioDriver(std::uint64_t base, std::size_t size, std::byte* backing)
        : Driver("mmio-test"),
          region_(MemoryRegion::Config{base, size, MemoryKind::Device, false},
                   backing) {}

    bool probe() noexcept override {
        set_state(neuro::dev::DriverState::Probing);
        return true;
    }

    bool start() noexcept override {
        set_state(neuro::dev::DriverState::Running);
        return true;
    }

    void stop() noexcept override {
        set_state(neuro::dev::DriverState::Stopped);
    }

    void regions(neuro::dev::Region* out,
                 std::size_t& n) noexcept override {
        if (n < 1) return;
        out[0] = {neuro::dev::RegionKind::MMIO,
                   region_.phys_base(), region_.size()};
        n = 1;
    }

    MemoryRegion& mem_region() noexcept { return region_; }

private:
    MemoryRegion region_;
};

// Mint a cap for the region using the kernel-side helper.
Capability make_cap(MemoryRegion& r, CapRight rights,
                     std::uint64_t gen = 1) {
    return Capability::mint(r.id(), rights,
                             /*epoch=*/0, gen);
}

}  // namespace

TEST(dev_cap, driver_uses_mmio_region_with_read_write_cap) {
    std::array<std::byte, 16> backing{};
    MmioDriver drv(REGION_OBJECT_ID_BASE, 16, backing.data());
    EXPECT_TRUE(drv.probe());
    EXPECT_TRUE(drv.start());

    auto& region = drv.mem_region();
    auto cap = make_cap(region, CapRight::Read | CapRight::Write);

    // Write a byte.
    EXPECT_TRUE(region.write_byte(0, 0xAB, cap));
    EXPECT_TRUE(region.write_byte(4, 0xCD, cap));

    // Read back.
    auto r0 = region.read_byte(0, cap);
    auto r4 = region.read_byte(4, cap);
    EXPECT_TRUE(r0.has_value());
    EXPECT_TRUE(r4.has_value());
    EXPECT_EQ(static_cast<std::uint8_t>(0xAB), *r0);
    EXPECT_EQ(static_cast<std::uint8_t>(0xCD), *r4);
}

TEST(dev_cap, write_rejected_without_write_cap) {
    std::array<std::byte, 4> backing{};
    MmioDriver drv(REGION_OBJECT_ID_BASE, 4, backing.data());
    drv.start();

    auto& region = drv.mem_region();
    auto read_only = make_cap(region, CapRight::Read);

    // Write should fail.
    EXPECT_FALSE(region.write_byte(0, 0xFF, read_only));
    // Read should succeed.
    EXPECT_TRUE(region.read_byte(0, read_only).has_value());
}

TEST(dev_cap, write_rejected_with_cap_for_other_region) {
    std::array<std::byte, 4> backing_a{};
    std::array<std::byte, 4> backing_b{};
    MmioDriver drv_a(REGION_OBJECT_ID_BASE, 4, backing_a.data());
    MmioDriver drv_b(REGION_OBJECT_ID_BASE + 0x1000, 4, backing_b.data());
    drv_a.start();
    drv_b.start();

    // Cap for drv_b — used against drv_a's region.
    auto wrong_cap = make_cap(drv_b.mem_region(),
                                CapRight::Read | CapRight::Write);

    EXPECT_FALSE(drv_a.mem_region().write_byte(0, 0xFE, wrong_cap));
    EXPECT_FALSE(drv_a.mem_region().read_byte(0, wrong_cap).has_value());
}

TEST(dev_cap, out_of_bounds_write_rejected) {
    std::array<std::byte, 4> backing{};
    MmioDriver drv(REGION_OBJECT_ID_BASE, 4, backing.data());
    drv.start();

    auto& region = drv.mem_region();
    auto cap = make_cap(region, CapRight::Read | CapRight::Write);

    // Byte write past end fails.
    EXPECT_FALSE(region.write_byte(4, 0xFF, cap));
    // u32 write at the end (offset 4 would exceed size) fails.
    EXPECT_FALSE(region.write_u32(4, 0xCAFEBABE, cap));
}

TEST(dev_cap, read_u32_in_bounds_succeeds) {
    std::array<std::byte, 8> backing{};
    MmioDriver drv(REGION_OBJECT_ID_BASE, 8, backing.data());
    drv.start();

    auto& region = drv.mem_region();
    auto cap = make_cap(region, CapRight::Read);

    // Both in-bounds reads succeed.
    EXPECT_TRUE(region.read_u32(0, cap).has_value());
    EXPECT_TRUE(region.read_u32(4, cap).has_value());
}

TEST(dev_cap, regions_call_advertises_matching_phys_base) {
    std::array<std::byte, 8> backing{};
    MmioDriver drv(REGION_OBJECT_ID_BASE + 0xCAFE, 8, backing.data());
    drv.start();

    neuro::dev::Region regs[4];
    std::size_t n = 4;
    drv.regions(regs, n);
    EXPECT_EQ(static_cast<std::size_t>(1), n);
    EXPECT_EQ(neuro::dev::RegionKind::MMIO, regs[0].kind);
    EXPECT_EQ(static_cast<std::uint64_t>(REGION_OBJECT_ID_BASE + 0xCAFE),
               regs[0].base);
    EXPECT_EQ(static_cast<std::uint64_t>(8), regs[0].size);
}

RUN_ALL_TESTS()