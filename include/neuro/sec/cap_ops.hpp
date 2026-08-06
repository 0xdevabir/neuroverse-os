// neuro/sec/cap_ops.hpp
//
// User-facing capability operations on a CapabilitySpace, per §4.2.
//
//   mint()    — kernel-only: create a new capability for an object
//   grant()   — transfer a capability from one space to another (or
//               duplicate within the same space) with optional
//               attenuation
//   attenuate() — derive a narrower capability from an existing one
//   revoke()  — invalidate every capability in the space by bumping
//               the epoch
//   verify()  — check a capability against the space's epoch + an
//               expected object_id + required rights
//
// Rights can only be reduced, never amplified.

#pragma once

#include <cstdint>
#include <optional>

#include "neuro/core/capability.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

namespace neuro::sec {

struct GrantResult {
    std::uint64_t handle;       // handle in the destination space
    bool          ok = false;
};

class CapOps {
public:
    // Kernel-style: create a brand-new capability for `object_id` with
    // `rights`, current epoch, and `generation` from the object.
    // Inserts into `space` and returns the new handle.
    [[nodiscard]] static std::uint64_t mint(CapabilitySpace& space,
                                            CapEpoch& epoch,
                                            std::uint64_t object_id,
                                            neuro::core::CapRight rights,
                                            std::uint64_t generation) {
        const auto ep = epoch.current();
        auto cap = neuro::core::Capability::mint(
            object_id, rights, ep, generation);
        return space.insert(cap);
    }

    // Look up a handle and verify it against the epoch + required rights.
    [[nodiscard]] static std::optional<neuro::core::Capability>
    resolve(const CapabilitySpace& space,
            const CapEpoch& epoch,
            std::uint64_t handle,
            neuro::core::CapRight required) {
        auto cap = space.lookup(handle);
        if (!cap) return std::nullopt;
        if (!epoch.valid(cap->epoch)) return std::nullopt;
        if (!cap->has(required))     return std::nullopt;
        return cap;
    }

    // Attenuate a capability to a strict subset of its rights. Returns
    // nullopt if the requested rights are not all already present.
    [[nodiscard]] static std::optional<neuro::core::Capability>
    attenuate(const neuro::core::Capability& cap,
              neuro::core::CapRight narrower) {
        if (!cap.has(narrower)) return std::nullopt;
        return cap.attenuate(narrower);
    }

    // Grant a capability into a destination space, with optional
    // attenuation. The source space's handle is consumed (erased) to
    // enforce the "capabilities move, don't copy" rule. To duplicate,
    // call grant() with `take = false`.
    [[nodiscard]] static GrantResult grant(CapabilitySpace& src,
                                           CapabilitySpace& dst,
                                           CapEpoch& src_epoch,
                                           std::uint64_t src_handle,
                                           neuro::core::CapRight narrower =
                                               neuro::core::CapRight::None,
                                           bool take = true) {
        GrantResult out{};
        auto cap = resolve(src, src_epoch, src_handle,
                           neuro::core::CapRight::Grant);
        if (!cap) return out;
        if (narrower != neuro::core::CapRight::None) {
            auto att = attenuate(*cap, narrower);
            if (!att) return out;
            cap = *att;
        }
        const auto h = dst.insert(*cap);
        if (h == kInvalidHandle) return out;
        if (take) src.erase(src_handle);
        out.handle = h;
        out.ok     = true;
        return out;
    }

    // Duplicate within the same space (or to another). Does not require
    // the Grant right — attenuation is the only gate.
    [[nodiscard]] static GrantResult duplicate(const CapabilitySpace& src,
                                               CapabilitySpace& dst,
                                               std::uint64_t src_handle,
                                               neuro::core::CapRight narrower =
                                                   neuro::core::CapRight::None) {
        GrantResult out{};
        auto cap = src.lookup(src_handle);
        if (!cap) return out;
        if (narrower != neuro::core::CapRight::None) {
            auto att = attenuate(*cap, narrower);
            if (!att) return out;
            cap = *att;
        }
        const auto h = dst.insert(*cap);
        if (h == kInvalidHandle) return out;
        out.handle = h;
        out.ok     = true;
        return out;
    }

    // Revoke every capability in `space` by bumping the epoch. After
    // this call, every previously-resolved capability returns nullopt
    // from resolve().
    static void revoke(CapabilitySpace& /*space*/, CapEpoch& epoch) {
        (void)epoch.revoke();
    }
};

}  // namespace neuro::sec