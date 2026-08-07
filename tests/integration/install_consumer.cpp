// tests/integration/install_consumer.cpp
//
// Phase P1.2 — standalone consumer of the staged install.
//
// This binary is compiled by tests/integration/install_consumer.sh
// against $(DESTDIR)/$(PREFIX)/include and $(DESTDIR)/$(PREFIX)/lib
// after `make install` makes them available. It is *not* compiled
// by the normal `make test` path — that would require the install to
// have already happened. Instead, the script invokes $(CXX) directly
// with the staged include path and links against libneuro_host.a.
//
// The consumer exercises a small surface from a handful of
// subsystems to prove the staged install is self-consistent and
// that cross-subsystem composition works against installed
// headers.

#include <cstdio>
#include <cstdlib>

#include "neuro/neuro.hpp"
#include "neuro/core/version.hpp"

int main() {
    // Print the version so the install shim confirms the version
    // header is reachable.
    std::printf("neuroverse-os consumer %s\n",
                neuro::core::version_string);

    // Stamp a capability — exercises neuro::core::Capability.
    auto cap = neuro::core::Capability::mint(
        /*object_id=*/0xC0DEC0DE,
        neuro::core::CapRight::Read | neuro::core::CapRight::Write,
        /*epoch=*/0, /*gen=*/1);
    if (cap.object_id != 0xC0DEC0DE) return 1;

    // Run a tiny IP render through the audio graph — exercises
    // neuro::audio::Graph + GainNode without touching the hardware.
    neuro::audio::Graph graph;
    graph.add_node(std::make_unique<neuro::audio::GainNode>(0.5f));
    if (!graph.prepare_all(48000, /*channels=*/2)) return 2;

    neuro::audio::Buffer in;
    in.frames = 4;
    in.channels = 2;
    in.samples.assign(8, 2.0f);
    std::vector<neuro::audio::Buffer> io;
    graph.render_block(std::span<const neuro::audio::Buffer>{&in, 1}, io);
    if (io.empty()) return 3;
    if (io[0].samples.empty()) return 4;
    if (io[0].samples[0] != 1.0f) return 5;  // 0.5 * 2.0

    // Hash a small payload via the SHA-3 helper — exercises the
    // package subsystem against the staged headers.
    auto digest = neuro::pkg::sha3::sha3_256(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("hello"), 5));
    if (digest.bytes.empty()) return 6;

    std::printf("consumer ok\n");
    return 0;
}
