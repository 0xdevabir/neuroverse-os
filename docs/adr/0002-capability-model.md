# ADR-0002: Capability Model

- **Status:** Accepted
- **Date:** 2026-08-07
- **Scope:** NeuroSec, NeuroCore, every subsystem that holds handles.

## Context

NeuroVerse ADR-0001 commits the system to a microkernel where
every cross-subsystem interaction is funneled through
capability-gated IPC. The capability model is therefore the
single source of truth for *who can do what to whom*. The README
§4.2 already sketches the broad shape (mint / grant / attenuate /
duplicate / revoke), but the precise semantics, the lifetime
guarantees, and the revocation protocol are not nailed down.

This ADR records the design choices that the implementation
already exposes through `neuro::core::Capability`,
`neuro::sec::cap_ops.hpp`, `neuro::sec::cap_space.hpp`, and
`neuro::sec::epoch.hpp`.

## Decision

A **capability** is a 16-byte unforgeable token of the following
shape:

```
struct Capability {
    std::uint64_t object_id;   // which kernel object
    std::uint64_t bits;        // rights(8) | epoch(16) | gen(8) | ...
};
```

The five primitive operations on a capability are:

| Operation | Effect on rights | Source cap required | Notes |
|-----------|------------------|---------------------|-------|
| `mint(object_id, rights)` | new | none (root) | produces a fresh cap with a unique epoch |
| `grant(src, dst_space, narrower, take=true)` | narrower ⊆ src | `CapRight::Grant` | move semantics on `take=true` |
| `grant(src, dst_space, narrower, take=false)` | narrower ⊆ src | `CapRight::Grant` | duplicate semantics |
| `duplicate(src, space, narrower)` | narrower ⊆ src | none | same space, no `Grant` required |
| `attenuate(src, narrower)` | narrower ⊆ src | none | same space, returns new cap |
| `revoke(space, handle)` | n/a | root | epoch bump; all old caps dead |

### Rights

Rights are a small bit-set:

```cpp
enum class CapRight : std::uint32_t {
    None   = 0,
    Read   = 1 << 0,
    Write  = 1 << 1,
    Exec   = 1 << 2,
    Grant  = 1 << 3,
    Map    = 1 << 4,
    DMA    = 1 << 5,
    Send   = 1 << 6,
    Recv   = 1 << 7,
};
```

Every attenuating operation must produce a `narrower` whose bits
are a strict subset of `src`. The runtime refuses over-attenuation
(`narrower ⊄ src`) and refuses narrowing to `CapRight::None` as a
no-op alias of the source.

### Generation counter

Each entry in `CapabilitySpace` carries a 8-bit `gen` counter. When
a handle is removed, the next insert at that slot uses
`gen + 1`. This collapses the decade-old "use-after-free" attack
in which stale handles continue to resolve after the slot was
reissued.

### Epoch-based revocation

`CapabilitySpace::revoke()` flips a 16-bit atomic `epoch_`. All
capabilities minted against the previous epoch become invalid;
`resolve()` checks the cap's epoch against the current epoch
before returning the kobject. This is the textbook Coyotos /
seL4 approach: revocation is O(1) at the cost of an extra field
on every cap.

The 16-bit epoch wraps at 65 536 revokes. The host scaffold
defines `epoch.valid(0)` after wrap to return true; this is
tested in `tests/unit/sec/epoch_test.cpp`.

### CapabilitySpace

A `CapabilitySpace` is a radix-trie (radix 16, depth 16) keyed by
the 64-bit `handle`. Maximum capacity is 2^16 = 65 536 entries.
Insertion returns a handle whose high 48 bits are the slot index
and whose low 16 bits are the generation; lookup is O(1) without
hash collisions.

### Kernel object IDs

`object_id` is the kernel's stable identifier for the underlying
object (endpoint, memory region, driver, etc.). The host
scaffold synthesises IDs from a 64-bit counter; the kernel
implementation will use the slot in the `KObjectTable`.

### Lifetime guarantee

A capability lives at least as long as some handle in some
`CapabilitySpace` references it. When the last reference is
dropped, the underlying object may be reclaimed by its owner's
destructor. `revoke()` does not destroy the object; it only
invalidates outstanding handles.

### `take=true` move semantics

`grant(src, dst_space, narrower, take=true)` removes the source
capability from the source space after the new (narrower) cap has
been inserted into the destination. This is the closest analogue
to `std::move()`. Because the move is performed atomically
(under the space mutex), the capability is observable in exactly
one space at the end of the operation.

## Consequences

### Positive

- **Capability isolation is end-to-end.** Every crossing of a
  trust boundary involves a cap check; there is no `uid_t` or
  `gid_t` override.
- **Revocation is cheap.** O(1) at the source; the cost is an
  extra word on every cap and an epoch check on every resolve.
- **Use-after-free is hard.** Re-use of a slot invalidates all
  stale handles via the generation counter.
- **Composition is uniform.** A subsystem handling a cap-carrying
  message does not need to know what kind of object the cap
  refers to; it just calls `resolve()` and the kernel does the
  rest.

### Negative

- **Cap size is two words.** This is fine on x86_64 / aarch64 /
  riscv64 but is doubled compared to a 64-bit cap. We accept
  the cost in exchange for the epoch + gen + rights bits.
- **Lost-cap leaks.** A capability that is dropped without
  `revoke()` continues to occupy its slot. The host scaffold
  exposes `CapabilitySpace::erase_if_dead()` for the user to
  drive reclamation; the kernel will swap that for a background
  collector.
- **Epoch wrap behaviour is subtle.** The 16-bit counter wraps
  and the `valid(0)` semantic is a single bit of convention that
  tests must lock in.

### Neutral

- **The model is consistent with E, seL4, Capsicum, and Coyotos.**
  Anyone familiar with those systems can read the cap-space code
  without surprises.

## Alternatives Considered

1. **64-bit capability with rights in low bits, no epoch.** Smaller
   but no revocation. Rejected.
2. **Pure software capability list (no generation).** Cheaper but
   vulnerable to the slot reissue use-after-free. Rejected.
3. **Password-capabilities (HKey).** Allow cap transfer across
   trust boundaries without involvement of the kernel. Deferred —
   might revisit for NeuroBridge.
4. **seL4-style 64-bit cap with kernel-managed revocation bit.**
   Cleaner but requires the kernel to be fully present. The
   host scaffold runs on machines without a kernel.

## References

- ADR-0001: Microkernel Design.
- `include/neuro/core/capability.hpp`.
- `include/neuro/sec/cap_ops.hpp`.
- `include/neuro/sec/cap_space.hpp`.
- `include/neuro/sec/epoch.hpp`.
- `tests/unit/sec/capability_test.cpp`,
  `tests/unit/sec/cap_space_test.cpp`,
  `tests/unit/sec/epoch_test.cpp`,
  `tests/unit/sec/cap_ops_test.cpp`.
- E programming language: capability semantics.
- seL4 formal model: capDL.
- Coyotos: epoch-based revocation.
- Capsicum: capability mode for POSIX.
