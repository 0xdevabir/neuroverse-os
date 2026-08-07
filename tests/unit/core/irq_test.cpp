// tests/unit/core/irq_test.cpp
//
// Tests for neuro::core::IrqSource — a capability-tracked IRQ
// dispatch primitive. Host scaffold only; the real PIC/APIC routing
// arrives with the kernel phase.
//
// Coverage:
//   - metadata (vector, trigger, polarity, shared)
//   - default handler_count is zero
//   - attach() adds a handler, count tracks
//   - clear() removes all handlers
//   - fire() invokes each handler in registration order
//   - fire() returns the number that returned true
//   - fire_count() increments on every fire()
//   - shared IRQs can have multiple handlers

#include "neuro/core/irq.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::IrqPolarity;
using neuro::core::IrqSource;
using neuro::core::IrqTrigger;

namespace {

IrqSource::Config cfg(std::uint32_t vector = 7,
                      IrqTrigger trig = IrqTrigger::Edge,
                      IrqPolarity pol = IrqPolarity::ActiveHigh,
                      bool shared = false) {
    IrqSource::Config c{};
    c.vector   = vector;
    c.trigger  = trig;
    c.polarity = pol;
    c.shared   = shared;
    return c;
}

}  // namespace

// ---- 1. metadata --------------------------------------------------

TEST(irq, metadata_defaults) {
    IrqSource s(cfg());
    EXPECT_EQ(static_cast<std::uint32_t>(7), s.vector());
    EXPECT_EQ(IrqTrigger::Edge,               s.trigger());
    EXPECT_EQ(IrqPolarity::ActiveHigh,        s.polarity());
    EXPECT_FALSE(s.shared());
}

TEST(irq, metadata_full) {
    IrqSource s(cfg(123, IrqTrigger::Level, IrqPolarity::ActiveLow, true));
    EXPECT_EQ(static_cast<std::uint32_t>(123), s.vector());
    EXPECT_EQ(IrqTrigger::Level,                s.trigger());
    EXPECT_EQ(IrqPolarity::ActiveLow,           s.polarity());
    EXPECT_TRUE(s.shared());
}

// ---- 2. default handler count is zero ----------------------------

TEST(irq, no_handlers_by_default) {
    IrqSource s(cfg());
    EXPECT_EQ(0u, s.handler_count());
    EXPECT_EQ(0u, s.fire());          // fire returns kept count = 0
    EXPECT_EQ(1u, s.fire_count());    // fires_ still increments
}

// ---- 3. attach + count --------------------------------------------

TEST(irq, attach_adds_handlers) {
    IrqSource s(cfg());
    s.attach([](IrqSource&) { return true; });
    s.attach([](IrqSource&) { return false; });
    EXPECT_EQ(2u, s.handler_count());

    s.attach([](IrqSource&) { return true; });
    EXPECT_EQ(3u, s.handler_count());
}

// ---- 4. clear removes all handlers -------------------------------

TEST(irq, clear_removes_handlers) {
    IrqSource s(cfg());
    s.attach([](IrqSource&) { return true; });
    s.attach([](IrqSource&) { return false; });
    EXPECT_EQ(2u, s.handler_count());
    s.clear();
    EXPECT_EQ(0u, s.handler_count());
}

// ---- 5. fire invokes handlers in registration order --------------

TEST(irq, fire_runs_handlers_in_order) {
    IrqSource s(cfg());
    std::vector<int> order;
    s.attach([&](IrqSource&) { order.push_back(1); return true; });
    s.attach([&](IrqSource&) { order.push_back(2); return true; });
    s.attach([&](IrqSource&) { order.push_back(3); return true; });

    s.fire();
    EXPECT_EQ(3u, order.size());
    EXPECT_EQ(1, order[0]);
    EXPECT_EQ(2, order[1]);
    EXPECT_EQ(3, order[2]);
}

// ---- 6. fire returns number of handlers that returned true -----

TEST(irq, fire_returns_kept_count) {
    IrqSource s(cfg());
    s.attach([](IrqSource&) { return true; });
    s.attach([](IrqSource&) { return false; });
    s.attach([](IrqSource&) { return true; });
    s.attach([](IrqSource&) { return false; });
    s.attach([](IrqSource&) { return true; });

    EXPECT_EQ(3u, s.fire());
}

// ---- 7. fire_count increments every fire() ----------------------

TEST(irq, fire_count_increments) {
    IrqSource s(cfg());
    EXPECT_EQ(0u, s.fire_count());
    s.fire();
    EXPECT_EQ(1u, s.fire_count());
    s.fire();
    s.fire();
    EXPECT_EQ(3u, s.fire_count());
}

// ---- 8. fire with no handlers does not crash + bumps fire_count

TEST(irq, fire_empty_irqs) {
    IrqSource s(cfg());
    EXPECT_EQ(0u, s.fire());
    EXPECT_EQ(1u, s.fire_count());
}

// ---- 9. handler can read source via reference --------------------

TEST(irq, handler_sees_source) {
    IrqSource s(cfg(99, IrqTrigger::Level, IrqPolarity::ActiveLow));
    std::uint32_t v = 0;
    IrqTrigger t{};
    IrqPolarity p{};
    s.attach([&](IrqSource& src) {
        v = src.vector();
        t = src.trigger();
        p = src.polarity();
        return true;
    });
    s.fire();
    EXPECT_EQ(static_cast<std::uint32_t>(99), v);
    EXPECT_EQ(IrqTrigger::Level,                t);
    EXPECT_EQ(IrqPolarity::ActiveLow,           p);
}

// ---- 10. clear + reattach works ----------------------------------

TEST(irq, clear_then_attach_again) {
    IrqSource s(cfg());
    s.attach([](IrqSource&) { return true; });
    s.clear();
    s.attach([](IrqSource&) { return false; });
    EXPECT_EQ(1u, s.handler_count());
    EXPECT_EQ(0u, s.fire());  // new handler returns false
}

// ---- 11. shared IRQ has multiple handlers ------------------------

TEST(irq, shared_has_multiple_handlers) {
    IrqSource s(cfg(11, IrqTrigger::Edge, IrqPolarity::ActiveHigh, true));
    EXPECT_TRUE(s.shared());

    std::atomic<int> a{0}, b{0}, c{0};
    s.attach([&](IrqSource&) { a.fetch_add(1); return true; });
    s.attach([&](IrqSource&) { b.fetch_add(1); return true; });
    s.attach([&](IrqSource&) { c.fetch_add(1); return true; });

    s.fire();
    EXPECT_EQ(1, a.load());
    EXPECT_EQ(1, b.load());
    EXPECT_EQ(1, c.load());
}

RUN_ALL_TESTS()