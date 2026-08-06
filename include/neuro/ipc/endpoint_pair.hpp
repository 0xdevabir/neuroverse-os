// neuro/ipc/endpoint_pair.hpp
//
// A pair of typed endpoints pre-wired for bidirectional use.
//
// Per README §4.1, a NeuroProcess typically has at least two
// endpoints: a control channel (parent -> child) and a notification
// channel (child -> parent). EndpointPair bundles the two so
// call sites do not have to wire up two separate Endpoint instances
// and risk getting the directions swapped.
//
// Convention (call-site responsibility):
//   - a().send(msg)  and  b().recv()        // a -> b
//   - b().send(msg)  and  a().recv()        // b -> a
//
// The two endpoints are independent queues; the *direction* is set
// by which side calls send and which calls recv. A future phase can
// tighten this with directional wrappers if the convention alone
// proves error-prone.

#pragma once

#include <memory>

#include "neuro/ipc/endpoint.hpp"

namespace neuro::ipc {

class EndpointPair {
public:
    EndpointPair()
        : a_(std::make_unique<Endpoint>()),
          b_(std::make_unique<Endpoint>()) {}

    Endpoint& a() noexcept { return *a_; }
    Endpoint& b() noexcept { return *b_; }

    // Test helpers.
    [[nodiscard]] std::size_t a_size() const { return a_->size(); }
    [[nodiscard]] std::size_t b_size() const { return b_->size(); }

private:
    std::unique_ptr<Endpoint> a_;
    std::unique_ptr<Endpoint> b_;
};

}  // namespace neuro::ipc
