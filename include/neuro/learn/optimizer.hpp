// neuro/learn/optimizer.hpp
//
// Optimizer skeleton (NeuroLearn, README §4.16).
//
// Per README §4.16 the OS is self-evolving: a learned optimizer
// observes runtime telemetry (queue depth, IPC latency, scheduler
// load) and proposes parameter changes (worker count, scheduling
// quantum, cache prefetch distance). On the host we expose the
// trait surface (Observation, Proposal, Optimizer); the real
// learned model + safe-application protocol lands in Phase 1.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace neuro::learn {

// One observation fed to the optimizer: a key/value snapshot of
// telemetry at a point in time. Values can be int, double, or a
// string label.
using ObservationValue = std::variant<std::int64_t, double, std::string>;

struct Observation {
    std::string                                  key;
    ObservationValue                             value;
    std::int64_t                                 timestamp_ns = 0;
};

// One optimizer proposal: change a knob. Safety is enforced by the
// kernel in Phase 1; here we expose the surface only.
struct Proposal {
    enum class Kind : std::uint8_t {
        SetWorkerCount,        // change worker thread count
        SetSchedulingQuantum,  // change scheduling quantum (µs)
        SetPrefetchDistance,   // change cache prefetch hint (pages)
        ToggleFeature,         // enable/disable a named feature
    };

    Kind         kind = Kind::SetWorkerCount;
    std::string  feature_name;       // for ToggleFeature
    std::int64_t integer_value = 0;  // for the first three kinds
};

// Optimizer trait. Concrete implementations observe telemetry,
// decide on a proposal, and emit it for the kernel to apply.
class Optimizer {
public:
    Optimizer()                       = default;
    Optimizer(const Optimizer&)       = delete;
    Optimizer& operator=(const Optimizer&) = delete;
    virtual ~Optimizer()              = default;

    // Feed one observation to the optimizer.
    virtual void observe(const Observation& o) = 0;

    // Feed many observations in one call.
    virtual void observe_many(const std::vector<Observation>& os) {
        for (auto& o : os) observe(o);
    }

    // Produce a proposal given the current observation window.
    // Returning a default-constructed Proposal means "no change".
    [[nodiscard]] virtual Proposal propose() const = 0;

    // Apply a proposal locally (no-op on the host; real impl in
    // Phase 1 negotiates with the kernel scheduler + driver bus).
    virtual void apply(const Proposal& /*p*/) {}

    // Diagnostic snapshot: human-readable state.
    [[nodiscard]] virtual std::string describe() const = 0;
};

// Singleton factory: one Optimizer per process.
Optimizer& host_optimizer();

// Helper: build a Proposal quickly.
inline Proposal make_set_workers(std::int64_t n) {
    Proposal p;
    p.kind = Proposal::Kind::SetWorkerCount;
    p.integer_value = n;
    return p;
}

inline Proposal make_set_quantum_us(std::int64_t q) {
    Proposal p;
    p.kind = Proposal::Kind::SetSchedulingQuantum;
    p.integer_value = q;
    return p;
}

inline Proposal make_toggle(std::string feature, bool enable) {
    Proposal p;
    p.kind = Proposal::Kind::ToggleFeature;
    p.feature_name = std::move(feature);
    p.integer_value = enable ? 1 : 0;
    return p;
}

}  // namespace neuro::learn