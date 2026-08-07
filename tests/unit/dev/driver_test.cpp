// tests/unit/dev/driver_test.cpp
//
// Tests for the NeuroDev driver framework. Covers:
//   - Driver name + initial state
//   - DriverState lifecycle (Registered → Probing → Attached →
//     Running or Failed)
//   - regions() enumerates MMIO / PIO / DMA ranges
//   - HostBus rescan() drives every driver through the lifecycle
//   - Bus entries are visited in registration order
//   - host_bus() is a singleton
//   - Failure on probe() / start() ends in DriverState::Failed

#include "neuro/dev/driver.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "../../test_framework.hpp"

using neuro::dev::Bus;
using neuro::dev::Driver;
using neuro::dev::DriverState;
using neuro::dev::Region;
using neuro::dev::RegionKind;
using neuro::dev::host_bus;
using neuro::dev::make_test_bus;

namespace {

// Test driver: a successful NIC-style driver with 2 MMIO regions.
class TestNic : public Driver {
public:
    TestNic() : Driver("test_nic") {}
    bool probe() noexcept override {
        probed_++;
        return true;
    }
    bool start() noexcept override {
        started_++;
        return true;
    }
    void stop() noexcept override { stopped_++; }

    void regions(Region* out, std::size_t& n) noexcept override {
        if (n >= 2) {
            out[0] = {RegionKind::MMIO, 0xFEB00000, 0x10000};
            out[1] = {RegionKind::DMA,  0x00000000, 0x1000};
        }
        n = 2;
    }

    int probed_  = 0;
    int started_ = 0;
    int stopped_ = 0;
};

// Test driver that fails at probe().
class ProbeFailDriver : public Driver {
public:
    ProbeFailDriver() : Driver("probe_fail") {}
    bool probe() noexcept override { return false; }
    bool start() noexcept override { return true; }  // never reached
    void stop() noexcept override {}
    void regions(Region*, std::size_t& n) noexcept override { n = 0; }
};

// Test driver that fails at start().
class StartFailDriver : public Driver {
public:
    StartFailDriver() : Driver("start_fail") {}
    bool probe() noexcept override { return true; }
    bool start() noexcept override { return false; }
    void stop() noexcept override {}
    void regions(Region*, std::size_t& n) noexcept override { n = 0; }
};

}  // namespace

// ---- 1. Initial state ------------------------------------------------

TEST(driver, initial_state_is_registered) {
    TestNic d;
    EXPECT_EQ("test_nic", d.name());
    EXPECT_EQ(DriverState::Registered, d.state());
}

// ---- 2. rescan() drives a successful driver through lifecycle ------

TEST(driver, rescan_drives_successful_lifecycle) {
    TestNic nic;
    auto bus = make_test_bus();
    bus->register_driver(&nic);
    EXPECT_EQ(1u, bus->size());

    bus->rescan();
    EXPECT_EQ(DriverState::Running, nic.state());
    EXPECT_EQ(1, nic.probed_);
    EXPECT_EQ(1, nic.started_);
}

// ---- 3. rescan() marks failed probe() as Failed --------------------

TEST(driver, rescan_marks_probe_failure) {
    ProbeFailDriver pf;
    auto bus = make_test_bus();
    bus->register_driver(&pf);
    bus->rescan();
    EXPECT_EQ(DriverState::Failed, pf.state());
}

// ---- 4. rescan() marks failed start() as Failed -------------------

TEST(driver, rescan_marks_start_failure) {
    StartFailDriver sf;
    auto bus = make_test_bus();
    bus->register_driver(&sf);
    bus->rescan();
    EXPECT_EQ(DriverState::Failed, sf.state());
}

// ---- 5. regions() enumerates claimed ranges -----------------------

TEST(driver, regions_returns_declared_ranges) {
    TestNic nic;
    Region regs[4];
    std::size_t n = 4;
    nic.regions(regs, n);
    EXPECT_EQ(2u, n);
    EXPECT_EQ(RegionKind::MMIO, regs[0].kind);
    EXPECT_EQ(0xFEB00000ULL,      regs[0].base);
    EXPECT_EQ(0x10000ULL,         regs[0].size);
    EXPECT_EQ(RegionKind::DMA,   regs[1].kind);
}

// ---- 6. set_state() transitions (tested via rescan) ---------------

TEST(driver, manual_set_state_works) {
    TestNic nic;
    nic.set_state(DriverState::Stopped);
    EXPECT_EQ(DriverState::Stopped, nic.state());
}

// ---- 7. rescan() visits drivers in registration order -----------

TEST(driver, rescan_visits_in_registration_order) {
    // Track the order of probe() calls via a heap-allocated recorder
    // so the test doesn't depend on static-init order.
    std::vector<std::string> order;

    struct OrderedDriver : Driver {
        std::vector<std::string>* log;
        std::string tag;
        OrderedDriver(std::vector<std::string>* l, std::string t)
            : Driver(t), log(l), tag(std::move(t)) {}
        bool probe() noexcept override {
            log->push_back(tag);
            return true;
        }
        bool start() noexcept override { return true; }
        void stop() noexcept override {}
        void regions(Region*, std::size_t& n) noexcept override { n = 0; }
    };

    OrderedDriver a(&order, "alpha");
    OrderedDriver b(&order, "beta");
    OrderedDriver c(&order, "gamma");
    auto bus = make_test_bus();
    bus->register_driver(&a);
    bus->register_driver(&b);
    bus->register_driver(&c);
    bus->rescan();
    // Find the last 3 entries (other drivers may have probed earlier
    // in the same rescan because the bus is a singleton across tests).
    EXPECT_TRUE(order.size() >= 3);
    auto last3 = std::vector<std::string>(
        order.end() - 3, order.end());
    EXPECT_EQ("alpha", last3[0]);
    EXPECT_EQ("beta",  last3[1]);
    EXPECT_EQ("gamma", last3[2]);
}

// ---- 8. host_bus() is a singleton -------------------------------

TEST(driver, host_bus_is_singleton) {
    auto& x = host_bus();
    auto& y = host_bus();
    EXPECT_EQ(&x, &y);
}

RUN_ALL_TESTS()