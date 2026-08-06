// neuro/proc/process.hpp
//
// Process — userspace-isolated container, per README §4.4.
//
// A Process bundles:
//   - a CapabilitySpace (root authority)
//   - a VMASpace (memory layout)
//   - a set of initial endpoints (one per kernel service the process
//     starts with; Phase F's EndpointPair provides the per-pair wiring)
//
// Host scaffold: Process is a non-virtual value type backed by the
// host's process address space. The kernel version becomes a kernel
// object that owns page tables, signal tables, and the loader state.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "neuro/core/endpoint.hpp"
#include "neuro/core/kobject.hpp"
#include "neuro/mem/vma_tree.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

namespace neuro::proc {

struct ProcessInit {
    std::string                 name;
    std::vector<std::string>    service_endpoints;   // service names
};

class Process : public core::KObject {
public:
    explicit Process(ProcessInit init);
    Process(const Process&)            = delete;
    Process& operator=(const Process&) = delete;

    [[nodiscard]] const std::string& name()    const noexcept { return init_.name; }
    [[nodiscard]] neuro::sec::CapabilitySpace& caps() noexcept { return caps_; }
    [[nodiscard]] const neuro::sec::CapabilitySpace& caps() const noexcept { return caps_; }
    [[nodiscard]] neuro::sec::CapEpoch& epoch() noexcept { return epoch_; }
    [[nodiscard]] neuro::mem::VMATree& vmas() noexcept { return vmas_; }
    [[nodiscard]] const neuro::mem::VMATree& vmas() const noexcept { return vmas_; }
    [[nodiscard]] std::vector<core::Endpoint*>& endpoints() noexcept { return endpoints_; }

    // Add an endpoint to this process's initial set. Returns a pointer
    // to the Endpoint so the caller can wire senders / receivers.
    core::Endpoint* add_endpoint(std::unique_ptr<core::Endpoint> ep) {
        endpoints_.push_back(ep.get());
        owned_.push_back(std::move(ep));
        return endpoints_.back();
    }

private:
    ProcessInit                           init_;
    neuro::sec::CapabilitySpace           caps_;
    neuro::sec::CapEpoch                  epoch_;
    neuro::mem::VMATree                   vmas_;
    std::vector<core::Endpoint*>          endpoints_;
    std::vector<std::unique_ptr<core::Endpoint>> owned_;
};

}  // namespace neuro::proc