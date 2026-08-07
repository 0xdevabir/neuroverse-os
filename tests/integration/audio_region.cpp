// tests/integration/audio_region.cpp
//
// Z7.10 — audio gain node writes samples into a memory-mapped region.
//
// The audio graph runs a GainNode over an interleaved float buffer
// and the post-render samples are written into a MemoryRegion's
// backing buffer. A second pass reads the region back and verifies
// the gain was applied.

#include "tests/test_framework.hpp"

#include "neuro/audio/pipeline.hpp"
#include "neuro/core/capability.hpp"
#include "neuro/core/io.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

using neuro::audio::Buffer;
using neuro::audio::GainNode;
using neuro::audio::Graph;
using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::core::MemoryKind;
using neuro::core::MemoryRegion;

namespace {

constexpr std::uint32_t kFrames   = 4;
constexpr std::uint32_t kChannels = 2;

// Write a Buffer's samples into a MemoryRegion byte-by-byte.
void write_buffer_to_region(MemoryRegion& region, Capability& cap,
                             const Buffer& buf) {
    auto* dst = region.raw();
    for (std::size_t i = 0; i < buf.samples.size(); ++i) {
        float f = buf.samples[i];
        std::memcpy(dst + i * sizeof(float), &f, sizeof(float));
    }
}

// Read samples back from a MemoryRegion into a Buffer.
Buffer read_region_to_buffer(MemoryRegion& region, Capability& cap,
                              std::uint32_t frames, std::uint32_t channels) {
    Buffer out;
    out.frames = frames;
    out.channels = channels;
    out.samples.resize(frames * channels);
    auto* src = region.raw();
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        float f;
        std::memcpy(&f, src + i * sizeof(float), sizeof(float));
        out.samples[i] = f;
    }
    return out;
}

}  // namespace

TEST(audio_region, gain_writes_to_memory_region) {
    // Allocate a backing buffer big enough for 4 frames × 2 channels
    // × 4 bytes (= 32 bytes).
    std::array<std::byte, 32> backing{};
    MemoryRegion region(
        MemoryRegion::Config{/*phys_base=*/0xA0000000,
                             /*size=*/32,
                             MemoryKind::Device,
                             /*prefetch=*/false},
        backing.data());

    Capability cap = Capability::mint(region.id(),
                                        CapRight::Read | CapRight::Write,
                                        /*epoch=*/0, /*gen=*/1);

    // Build a graph with a gain of 2.0.
    Graph g;
    g.add_node(std::make_unique<GainNode>(2.0f));
    EXPECT_TRUE(g.prepare_all(48000, kChannels));

    // Source: 4 frames × 2 channels of 1.0s.
    Buffer in;
    in.frames = kFrames;
    in.channels = kChannels;
    in.samples.assign(kFrames * kChannels, 1.0f);

    std::vector<Buffer> io;
    g.render_block(std::span<const Buffer>{&in, 1}, io);

    EXPECT_EQ(static_cast<std::size_t>(1), io.size());
    Buffer& out = io[0];
    EXPECT_EQ(static_cast<std::size_t>(kFrames * kChannels), out.samples.size());
    for (float s : out.samples) {
        EXPECT_EQ(2.0f, s);
    }

    // Write the output buffer into the MemoryRegion.
    write_buffer_to_region(region, cap, out);

    // Read back.
    Buffer roundtrip = read_region_to_buffer(region, cap, kFrames, kChannels);
    EXPECT_EQ(out.samples.size(), roundtrip.samples.size());
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        EXPECT_EQ(out.samples[i], roundtrip.samples[i]);
    }
}

TEST(audio_region, gain_chain_then_region) {
    std::array<std::byte, 32> backing{};
    MemoryRegion region(
        MemoryRegion::Config{0xA0000000, 32, MemoryKind::Device, false},
        backing.data());

    Capability cap = Capability::mint(region.id(),
                                        CapRight::Read | CapRight::Write,
                                        0, 1);

    // Two gain nodes in series: 0.5 * 0.5 = 0.25 applied.
    Graph g;
    g.add_node(std::make_unique<GainNode>(0.5f));
    g.add_node(std::make_unique<GainNode>(0.5f));
    EXPECT_TRUE(g.prepare_all(48000, kChannels));

    Buffer in;
    in.frames = kFrames;
    in.channels = kChannels;
    in.samples.assign(kFrames * kChannels, 4.0f);

    std::vector<Buffer> io;
    g.render_block(std::span<const Buffer>{&in, 1}, io);

    // Each gain halves, so output should be 1.0.
    Buffer& out = io.back();
    for (float s : out.samples) {
        EXPECT_EQ(1.0f, s);
    }

    write_buffer_to_region(region, cap, out);
    Buffer roundtrip = read_region_to_buffer(region, cap, kFrames, kChannels);
    EXPECT_EQ(out.samples.size(), roundtrip.samples.size());
}

TEST(audio_region, region_writes_gated_by_cap) {
    std::array<std::byte, 32> backing{};
    MemoryRegion region(
        MemoryRegion::Config{0xA0000000, 32, MemoryKind::Device, false},
        backing.data());

    Capability read_only = Capability::mint(region.id(),
                                              CapRight::Read, 0, 1);

    // Try to write a sample with a read-only cap → fail.
    EXPECT_FALSE(region.write_byte(0, 0xFF, read_only));
    // Read succeeds.
    EXPECT_TRUE(region.read_byte(0, read_only).has_value());
}

RUN_ALL_TESTS()