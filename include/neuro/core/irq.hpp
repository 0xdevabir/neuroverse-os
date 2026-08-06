// neuro/core/irq.hpp
//
// InterruptSource: capability-gated IRQ object.
//
// Host scaffold: an IRQ source is a callable handler registered on a
// KObject::Kind::IrqSource id. Real hardware IRQ routing (PIC/APIC on
// x86, GIC on ARM) arrives in Phase 1 when the kernel boots. For now
// it's a typed dispatch primitive useful for userspace event sources
// (timer ticks, IPC notifications, etc).

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "neuro/core/kobject.hpp"

namespace neuro::core {

enum class IrqTrigger : std::uint8_t {
    Edge   = 0,
    Level  = 1,
};

enum class IrqPolarity : std::uint8_t {
    ActiveHigh = 0,
    ActiveLow  = 1,
};

class IrqSource;  // forward decl required by IrqHandler

// A handler is invoked when the IRQ fires. Return true to keep the IRQ
// masked on level-triggered lines, false to ack and re-arm.
using IrqHandler = std::function<bool(IrqSource&)>;

class IrqSource : public KObject {
public:
    struct Config {
        std::uint32_t vector    = 0;     // IRQ line number (host: synthetic)
        IrqTrigger    trigger   = IrqTrigger::Edge;
        IrqPolarity   polarity  = IrqPolarity::ActiveHigh;
        bool          shared    = false;
    };

    explicit IrqSource(Config cfg)
        : KObject(KObjectKind::IrqSource), cfg_(cfg) {}

    [[nodiscard]] std::uint32_t vector()  const noexcept { return cfg_.vector; }
    [[nodiscard]] IrqTrigger    trigger() const noexcept { return cfg_.trigger; }
    [[nodiscard]] IrqPolarity   polarity()const noexcept { return cfg_.polarity; }
    [[nodiscard]] bool          shared()  const noexcept { return cfg_.shared; }

    // Set/clear the user-supplied handler. Multiple handlers may be
    // installed on a shared IRQ; the dispatch order is registration
    // order.
    void attach(IrqHandler h) {
        std::lock_guard lk(mu_);
        handlers_.push_back(std::move(h));
    }

    void clear() {
        std::lock_guard lk(mu_);
        handlers_.clear();
    }

    [[nodiscard]] std::size_t handler_count() const {
        std::lock_guard lk(mu_);
        return handlers_.size();
    }

    // Fire the IRQ. Returns the number of handlers that returned true.
    // In a real kernel this is invoked from the IRQ entry path; here it
    // is callable by tests and by the timer subsystem.
    std::size_t fire() {
        std::vector<IrqHandler> snapshot;
        {
            std::lock_guard lk(mu_);
            snapshot = handlers_;
        }
        std::size_t kept = 0;
        for (auto& h : snapshot) {
            if (h(*this)) ++kept;
        }
        fires_.fetch_add(1, std::memory_order_relaxed);
        return kept;
    }

    [[nodiscard]] std::uint64_t fire_count() const noexcept {
        return fires_.load(std::memory_order_relaxed);
    }

private:
    Config                  cfg_;
    mutable std::mutex      mu_;
    std::vector<IrqHandler> handlers_;
    std::atomic<std::uint64_t> fires_{0};
};

}  // namespace neuro::core