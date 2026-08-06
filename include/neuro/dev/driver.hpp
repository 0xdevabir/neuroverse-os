// neuro/dev/driver.hpp
//
// Driver framework skeleton.
//
// Per README §4.8 (NeuroDev):
//   - Drivers are normal user processes that hold a cap-gated
//     handle to one or more IO regions (MMIO / PIO / DMA).
//   - The kernel exposes an attach point (a per-bus object table)
//     and the driver registers a probe / start / stop triple.
//   - Capability gating means a misbehaving driver can only
//     fault its own MMIO regions; the rest of the system is
//     untouched.
//
// On the host scaffold we keep the trait surface so the rest of
// NeuroDev (PCIe scan, IRQ routing, DMA maps) can be wired
// against a stable interface. The kernel implementation will
// drop in for Phase 1.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace neuro::dev {

// IO region kinds: matches the kernel-side BDF discriminator.
enum class RegionKind : std::uint8_t {
    MMIO,    // memory-mapped IO
    PIO,     // port-mapped IO (x86 legacy)
    DMA,     // bus-master DMA window
};

// One address range the driver can touch.
struct Region {
    RegionKind kind = RegionKind::MMIO;
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

// Driver lifecycle states.
enum class DriverState : std::uint8_t {
    Registered,   // declared, not yet attached
    Probing,      // running probe()
    Attached,     // probe() returned success; start() not yet called
    Running,      // start() succeeded; serving interrupts
    Stopped,      // stop() called; safe to unload
    Failed,       // probe or start returned an error
};

// One concrete driver. The trait gives NeuroDev a uniform way to
// bind drivers to buses without dragging in the concrete
// implementation type.
class Driver {
public:
    Driver() = default;
    explicit Driver(std::string name) : name_(std::move(name)) {}
    virtual ~Driver() = default;

    // Read-only metadata.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // Lifecycle hooks. probe() inspects the bus and decides whether
    // the device is present; start() enables interrupts and
    // finishes setup; stop() disables them in reverse order.
    virtual bool probe()   noexcept = 0;
    virtual bool start()   noexcept = 0;
    virtual void stop()    noexcept = 0;

    // The MMIO / PIO / DMA regions the driver claims. The kernel
    // uses this to mint cap-gated handles on attach.
    virtual void regions(Region* out, std::size_t& n) noexcept = 0;

    [[nodiscard]] DriverState state() const noexcept { return state_; }

    // Test helpers / kernel-side driver loader entry points.
    void set_state(DriverState s) noexcept { state_ = s; }

private:
    std::string  name_;
    DriverState  state_ = DriverState::Registered;
};

// One entry on a bus. The bus owns the drivers and walks them in
// order on every rescan.
struct BusEntry {
    std::uint16_t vendor = 0;
    std::uint16_t device = 0;
    Driver*       driver = nullptr;   // non-owning; lifetime managed
                                       // by the bus.
};

// Top-level bus: PCIe, USB, platform, etc. On the host scaffold a
// single host-bus instance stands in.
class Bus {
public:
    virtual ~Bus() = default;

    virtual void register_driver(Driver* d) = 0;
    virtual void rescan()                    = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

// Singleton-ish factory for the host bus.
Bus& host_bus();

}  // namespace neuro::dev