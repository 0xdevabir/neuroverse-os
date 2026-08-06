# NeuroVerse OS — Public API Documentation

This directory holds generated Doxygen output for NeuroVerse OS.

## Generating

The output is generated from the public headers under `include/neuro/`
plus the demo binaries and ADRs:

```bash
doxygen docs/Doxyfile
open docs/api/html/index.html
```

The Doxyfile in this directory's parent (`docs/Doxyfile`) reads:

- All `*.hpp` / `*.cpp` under `include/neuro/` and `src/scratch/`
- All `*.md` (including the README and ADRs)
- Excludes `third_party/`, `build/`, and `tests/`

## Output layout

```
docs/api/
├── html/                  ← browseable HTML (open index.html)
│   ├── index.html
│   ├── annotated.html     ← every public type, alphabetical
│   ├── files.html         ← every documented file
│   ├── namespaces.html    ← subsystem namespaces
│   └── ...
├── xml/                   ← machine-readable (for Sphinx/Breathe/etc.)
└── man/
```

## Conventions

The Doxyfile enables Markdown in docs and reads Javadoc-style
`@brief` automatically. Prefer the C++-style one-line summary
above a declaration:

```cpp
/// Allocate `n` bytes at `align` boundary. Returns nullptr if the
/// arena is exhausted.
std::byte* allocate(std::size_t n, std::size_t align = 16);
```

For longer docs, use Markdown blocks separated from the
declaration by a blank line.

## Subsystem navigation

The public API is grouped by README §4. The umbrella header
`<neuro/neuro.hpp>` pulls them all in; production code should
include only the headers it needs. From the HTML sidebar:

- **Core** — `core::Result`, `core::Capability`, `core::Endpoint`
- **Sec** — `sec::CapabilitySpace`, `sec::Epoch`, `sec::cap_ops`
- **Mem** — `mem::Arena`, `mem::Pool`, `mem::VmaTree`
- **Proc / Sched** — `proc::Process`, `proc::Thread`, `sched::*`
- **IPC** — `ipc::Endpoint`, `ipc::EndpointPair`, `ipc::Message`
- **Net** — `net::IpAddr`, `net::UdpSocket`, `net::TcpSocket`, `net::Dns`
- **FS** — `fs::VFS`, `fs::VNode`, `fs::MemFS`, `fs::OverlayFS`
- **Dev / UI / Audio** — driver framework, compositor, DSP graph
- **Fabric / Pkg / JIT** — cluster membership, CAS, JIT engine
- **Proof / Pulse / Learn** — contracts, telemetry, learned optimizer
- **Bridge / Boot** — FFI, boot protocol