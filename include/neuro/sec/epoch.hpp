// neuro/sec/epoch.hpp
//
// Capability revocation via epoch counters, per §4.2.
//
// Each CapabilitySpace has a CapEpoch. Every capability minted in the
// space records the epoch it was minted at. The kernel bumps the space's
// epoch on revocation; any capability whose recorded epoch is older
// than the current epoch is invalid.
//
// The Capability struct has a 16-bit `epoch` field (§9.2), so we
// restrict each space to 65 536 revocation cycles before wrap-around.
// A wrap-around would re-validate stale capabilities, so the kernel must
// retire the entire space at the wrap boundary. For now we just expose
// `current()` and `revoke()` and let the kernel layer enforce the limit.

#pragma once

#include <atomic>
#include <cstdint>

namespace neuro::sec {

class CapEpoch {
public:
    CapEpoch() = default;

    // Monotonic counter. Wraps at 2^16; caller is responsible for retiring
    // the space on wrap.
    [[nodiscard]] std::uint16_t current() const noexcept {
        return epoch_.load(std::memory_order_acquire);
    }

    // Bump the epoch and return the new value.
    [[nodiscard]] std::uint16_t revoke() noexcept {
        const auto next = static_cast<std::uint16_t>(
            epoch_.load(std::memory_order_relaxed) + 1);
        epoch_.store(next, std::memory_order_release);
        return next;
    }

    // Test whether a capability minted at `cap_epoch` is still valid in
    // the current epoch. Equal means valid; less-than means revoked.
    [[nodiscard]] bool valid(std::uint16_t cap_epoch) const noexcept {
        return current() == cap_epoch;
    }

private:
    std::atomic<std::uint16_t> epoch_{0};
};

}  // namespace neuro::sec