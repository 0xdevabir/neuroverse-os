// tests/unit/audio/pipeline_test.cpp
//
// Tests for the NeuroAudio DSP pipeline. Covers:
//   - Buffer + frame/channel invariants
//   - GainNode scales amplitude
//   - PassthroughNode copies input
//   - Graph::prepare_all() / render_block() chained execution
//   - Graph::render_block() with no input still produces output
//   - prepare() returning false aborts the graph
//   - SumNode mixes two inputs into one (new in U1)

#include "neuro/audio/pipeline.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "../../test_framework.hpp"

using neuro::audio::Buffer;
using neuro::audio::GainNode;
using neuro::audio::Graph;
using neuro::audio::Node;
using neuro::audio::PassthroughNode;

namespace {

// Helper: build a Buffer with N frames of `channels` interleaved
// samples filled by a lambda. The lambda receives (frame, channel).
Buffer make_buffer(std::uint32_t frames, std::uint32_t channels,
                   float fill_value) {
    Buffer b;
    b.frames = frames;
    b.channels = channels;
    b.samples.assign(frames * channels, fill_value);
    return b;
}

// SumNode: averages every input channel-by-channel into the
// output. New in U1 — the only test that exercises a multi-input
// node, since the rest of the pipeline takes one input.
class SumNode : public Node {
public:
    SumNode() : Node("sum") {}

    bool prepare(std::uint32_t, std::uint32_t) noexcept override {
        return true;
    }
    void render(std::span<const Buffer> in,
                std::span<Buffer>       out) noexcept override {
        if (out.empty()) return;
        auto& dst = out.front();
        // Initialize dst with zeros matching the first input's shape.
        if (!in.empty()) {
            const auto& src = in.front();
            dst.channels = src.channels;
            dst.frames   = src.frames;
            dst.samples.assign(src.samples.size(), 0.0f);
        }
        for (const auto& src : in) {
            const std::size_t n = std::min(dst.samples.size(),
                                            src.samples.size());
            for (std::size_t i = 0; i < n; ++i) {
                dst.samples[i] += src.samples[i];
            }
        }
    }
};

// FailingNode: a test-only node whose prepare() returns false.
class FailingNode : public Node {
public:
    FailingNode() : Node("fail") {}
    bool prepare(std::uint32_t, std::uint32_t) noexcept override {
        return false;
    }
    void render(std::span<const Buffer>, std::span<Buffer>) noexcept override {}
};

}  // namespace

// ---- 1. Buffer invariants --------------------------------------------

TEST(audio, buffer_size_is_frames_times_channels) {
    Buffer b = make_buffer(/*frames=*/4, /*channels=*/2, 0.0f);
    EXPECT_EQ(8u, b.samples.size());
    EXPECT_EQ(2u, b.channels);
    EXPECT_EQ(4u, b.frames);
}

// ---- 2. GainNode scales -----------------------------------------------

TEST(audio, gain_node_scales_amplitude) {
    GainNode g(0.5f);
    Buffer in  = make_buffer(2, 2, 1.0f);
    g.prepare(48000, 2);
    Buffer dst;
    std::vector<Buffer> in_v{in};
    std::span<const Buffer> in_span{in_v};
    std::span<Buffer> out_span{&dst, 1};
    g.render(in_span, out_span);
    EXPECT_EQ(2u, dst.frames);
    EXPECT_EQ(0.5f, dst.samples[0]);
    EXPECT_EQ(0.5f, dst.samples[3]);
}

// ---- 3. PassthroughNode copies input ---------------------------------

TEST(audio, passthrough_copies_input) {
    PassthroughNode p;
    Buffer in  = make_buffer(2, 2, 0.42f);
    Buffer dst;
    std::vector<Buffer> in_v{in};
    std::span<const Buffer> in_span{in_v};
    std::span<Buffer> out_span{&dst, 1};
    p.render(in_span, out_span);
    EXPECT_EQ(2u, dst.frames);
    EXPECT_EQ(0.42f, dst.samples[0]);
    EXPECT_EQ(0.42f, dst.samples[3]);
}

// ---- 4. Graph: gain then passthrough --------------------------------

TEST(audio, graph_gain_then_passthrough) {
    Graph g;
    g.add_node(std::make_unique<GainNode>(2.0f));
    g.add_node(std::make_unique<PassthroughNode>());
    EXPECT_TRUE(g.prepare_all(48000, 2));
    EXPECT_TRUE(g.prepared());

    Buffer in = make_buffer(2, 2, 0.5f);  // 0.5 * 2.0 = 1.0
    std::vector<Buffer> io;
    std::vector<Buffer> in_v{in};
    std::span<const Buffer> in_span{in_v};
    g.render_block(in_span, io);
    // Two nodes → two output buffers.
    EXPECT_EQ(2u, io.size());
    // First is gain: 1.0; second is passthrough of that: 1.0.
    EXPECT_EQ(1.0f, io[0].samples[0]);
    EXPECT_EQ(1.0f, io[1].samples[0]);
}

// ---- 5. Graph with no input still produces output -------------------

TEST(audio, graph_empty_input_still_renders) {
    Graph g;
    g.add_node(std::make_unique<GainNode>(0.5f));
    EXPECT_TRUE(g.prepare_all(48000, 2));
    std::vector<Buffer> io;
    std::vector<Buffer> in_v;
    std::span<const Buffer> in_span{in_v};
    g.render_block(in_span, io);
    EXPECT_EQ(1u, io.size());
}

// ---- 6. Graph prepare fails if any node fails -----------------------

TEST(audio, graph_prepare_fails_if_node_fails) {
    Graph g;
    g.add_node(std::make_unique<GainNode>(1.0f));
    g.add_node(std::make_unique<FailingNode>());
    EXPECT_FALSE(g.prepare_all(48000, 2));
    EXPECT_FALSE(g.prepared());
}

// ---- 7. Unprepared graph does nothing on render ---------------------

TEST(audio, unprepared_graph_render_is_noop) {
    Graph g;
    g.add_node(std::make_unique<GainNode>(2.0f));
    std::vector<Buffer> io;
    std::vector<Buffer> in_v{make_buffer(2, 2, 1.0f)};
    std::span<const Buffer> in_span{in_v};
    g.render_block(in_span, io);
    EXPECT_EQ(0u, io.size());
}

// ---- 8. SumNode mixes two inputs ------------------------------------

TEST(audio, sum_node_mixes_two_inputs) {
    SumNode s;
    s.prepare(48000, 2);
    Buffer a = make_buffer(2, 2, 0.25f);
    Buffer b = make_buffer(2, 2, 0.5f);
    Buffer dst;
    std::vector<Buffer> in_v{a, b};
    std::span<const Buffer> in_span{in_v};
    std::span<Buffer> out_span{&dst, 1};
    s.render(in_span, out_span);
    EXPECT_EQ(2u, dst.frames);
    EXPECT_EQ(0.75f, dst.samples[0]);  // 0.25 + 0.5
    EXPECT_EQ(0.75f, dst.samples[3]);
}

// ---- 9. SumNode with one input is just a copy -----------------------

TEST(audio, sum_node_with_one_input_copies) {
    SumNode s;
    s.prepare(48000, 2);
    Buffer a = make_buffer(2, 2, 0.5f);
    Buffer dst;
    std::vector<Buffer> in_v{a};
    std::span<const Buffer> in_span{in_v};
    std::span<Buffer> out_span{&dst, 1};
    s.render(in_span, out_span);
    EXPECT_EQ(0.5f, dst.samples[0]);
    EXPECT_EQ(0.5f, dst.samples[3]);
}

// ---- 10. Graph: gain then sum (verifies multi-input wiring) --------

TEST(audio, graph_with_sum_node) {
    Graph g;
    g.add_node(std::make_unique<GainNode>(2.0f));
    // The graph only feeds the previous node's output into each
    // node; SumNode's render treats that as a single input. We
    // verify it copies correctly here.
    g.add_node(std::make_unique<SumNode>());
    EXPECT_TRUE(g.prepare_all(48000, 2));
    Buffer in = make_buffer(2, 2, 0.25f);  // 0.25 * 2 = 0.5
    std::vector<Buffer> io;
    std::vector<Buffer> in_v{in};
    std::span<const Buffer> in_span{in_v};
    g.render_block(in_span, io);
    EXPECT_EQ(2u, io.size());
    EXPECT_EQ(0.5f, io[0].samples[0]);
    EXPECT_EQ(0.5f, io[1].samples[0]);  // sum copies
}

RUN_ALL_TESTS()