// src/dev/driver.cpp
//
// Host bus singleton. Concrete drivers (NIC, GPU, audio) attach
// here; the kernel implementation drops this for a real bus
// driver stack.

#include "neuro/dev/driver.hpp"

#include <vector>

namespace neuro::dev {

namespace {

class HostBus : public Bus {
public:
    void register_driver(Driver* d) override {
        drivers_.push_back(d);
    }

    void rescan() override {
        for (auto* d : drivers_) {
            d->set_state(DriverState::Probing);
            if (!d->probe()) {
                d->set_state(DriverState::Failed);
                continue;
            }
            d->set_state(DriverState::Attached);
            if (!d->start()) {
                d->set_state(DriverState::Failed);
                continue;
            }
            d->set_state(DriverState::Running);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        return drivers_.size();
    }

private:
    std::vector<Driver*> drivers_;  // non-owning
};

HostBus& host_bus_singleton() {
    static HostBus b;
    return b;
}

}  // namespace

Bus& host_bus() { return host_bus_singleton(); }

}  // namespace neuro::dev
