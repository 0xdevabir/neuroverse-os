# ADR-0001: Microkernel Design

- **Status:** Accepted
- **Date:** 2026-08-07
- **Scope:** NeuroCore, NeuroSec, all kernel-space subsystems.

## Context

NeuroVerse OS must support a heterogeneous mix of workloads
(graphics, audio, network, ML inference, distributed fabric) on a
single binary across x86_64, aarch64, and riscv64. The dominant
historical choice between operating-system architectures is the
**monolithic kernel** (Linux, BSD) versus the **microkernel**
(Minix, seL4, QNX). A third option — **hybrid kernels** (Windows
NT, XNU) — sits between them.

The README §4 already enumerates 16+ subsystems with stated
boundaries; this ADR records the architectural shape and the
trade-offs it commits us to.

## Decision

NeuroVerse will be a **microkernel**: only NeuroCore (capability
tables, scheduler, IPC, IRQ dispatch, page tables) and NeuroSec
(cap mint / attenuate / revoke) execute in supervisor mode. Every
other subsystem — NeuroFS, NeuroDev, NeuroNet, NeuroUI,
NeuroAudio, NeuroPkg, NeuroJIT, NeuroFabric, NeuroProof,
NeuroPulse, NeuroLearn, NeuroBridge, NeuroBoot — runs as
**isolated user-space servers** that hold only the capabilities
they were granted.

Communication between servers happens exclusively through
**capability-gated IPC endpoints** (NeuroCore.ipc). A network
driver, for example, holds a `CapRight::DMA` over its ring buffer
and a `CapRight::Send` to the network stack's endpoint; nothing
else.

Bootstrapping is done by a minimal NeuroBoot loader that hands the
first capability bundle to the init process. From there the
graph of services is built lazily and revocation-aware.

## Consequences

### Positive

- **No kernel-mode code beyond the proven core.** Every filesystem,
  driver, and protocol stack runs as a process that can be
  stopped, restarted, and replaced without rebooting.
- **Capability security is end-to-end.** A bug in a driver cannot
  read arbitrary memory: it can only touch what its capabilities
  describe.
- **Multi-arch cost is paid once.** Per-arch porting work
  concentrates on the small NeuroCore layer.
- **Live update** of services becomes possible (NeuroLearn can
  swap a subsystem out at runtime if the new image is
  capability-equivalent).

### Negative

- **IPC overhead.** Cross-subsystem calls cost at least one
  capability check + thread hop. We mitigate with `co_await`
  in the IPC layer and zero-copy ring buffers where the data
  plane warrants it.
- **Sharing semantics.** Pages shared between two services need
  explicit `CapRight::Map` grants; the convenient mmap-style
  sharing of monolithic kernels is gone.
- **Boot complexity.** The init graph must be describable in
  capability terms, not just a list of files to exec.

### Neutral

- **Aligned with the README roadmap.** §4.1 already specifies
  capability-secure IPC + capability primitives as the spine.

## Alternatives Considered

1. **Monolithic kernel.** Faster cross-component calls but loses
   capability isolation across subsystems; a bug in the VFS
   could read any process's memory. Rejected.
2. **Hybrid kernel (kernel threads for drivers).** Better
   performance than microkernel, worse than monolithic, but
   loses capability isolation for drivers that hold kernel
   state. Considered for `NeuroDev`, deferred — drivers also
   live in user space under this ADR.

## References

- seL4 formal verification report.
- Hurd / Mach microkernel design papers.
- README §4 (subsystem boundaries), §4.1 (NeuroCore), §4.2
  (NeuroSec), §4.18 (NeuroBoot).
