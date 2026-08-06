// neuro/audio/pipeline.hpp
//
// Audio DSP pipeline skeleton.
//
// Per README §4.10 (NeuroAudio):
//   - Audio is a directed acyclic graph of Nodes. Each node
//     consumes N input buffers and produces M output buffers
//     per render().
//   - Sources (file reader, capture) feed sinks (output device,
//     encoder); effects (filter, gain, mixer) sit between.
//   - The DSP graph runs on a dedicated thread (or set of
//     threads) coordinated by the audio server.
//
// On the host scaffold we keep the Node trait + a simple Graph
// that owns nodes in topological order. The kernel audio
// subsystem replaces this with the real interrupt-driven path.

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace neuro::audio {

// Interleaved float32 frames at a fixed sample rate.
struct Buffer {
    std::vector<float> samples;        // interleaved L,R,L,R,...
    std::uint32_t      channels = 2;
    std::uint32_t      frames   = 0;   // per channel
};

// Node: a single DSP block. The graph calls prepare() once at
// load time and render() every block.
class Node {
public:
    explicit Node(std::string name) : name_(std::move(name)) {}
    virtual ~Node() = default;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // Called once before the graph starts. Returns false if the
    // node cannot satisfy its configuration (e.g., unsupported
    // channel count).
    virtual bool prepare(std::uint32_t sample_rate_hz,
                         std::uint32_t channels) noexcept = 0;

    // Render one block. Inputs and outputs are non-overlapping;
    // the node may read inputs and overwrite outputs in place.
    virtual void render(std::span<const Buffer> in,
                         std::span<Buffer>       out) noexcept = 0;

private:
    std::string name_;
};

// A small graph container that owns its nodes and runs them in
// declared order. Inputs of node N come from outputs of node N-1.
class Graph {
public:
    Graph() = default;

    void add_node(std::unique_ptr<Node> n) {
        nodes_.push_back(std::move(n));
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes_.size();
    }

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }

    bool prepare_all(std::uint32_t sample_rate_hz,
                     std::uint32_t channels) {
        for (auto& n : nodes_) {
            if (!n->prepare(sample_rate_hz, channels)) return false;
        }
        prepared_ = true;
        return true;
    }

    // Run one block through every node in order.
    void render_block(std::span<const Buffer> in,
                      std::vector<Buffer>& io) {
        if (!prepared_ || nodes_.empty()) return;
        io.resize(nodes_.size());
        // Seed with the source (input from caller).
        if (!in.empty()) io.front() = in.front();
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            std::span<const Buffer> src_in;
            std::span<Buffer>       dst_out{&io[i], 1};
            if (i == 0 && !in.empty()) {
                src_in = in;
            } else if (i > 0) {
                src_in = std::span<const Buffer>(&io[i - 1], 1);
            }
            nodes_[i]->render(src_in, dst_out);
        }
    }

private:
    std::vector<std::unique_ptr<Node>> nodes_;
    bool                               prepared_ = false;
};

// Built-in nodes: a simple gain stage and a passthrough.
class GainNode : public Node {
public:
    explicit GainNode(float gain) : Node("gain"), gain_(gain) {}

    bool prepare(std::uint32_t, std::uint32_t) noexcept override {
        return true;
    }
    void render(std::span<const Buffer> in,
                std::span<Buffer>       out) noexcept override {
        if (in.empty() || out.empty()) return;
        const auto& src = in.front();
        auto&       dst = out.front();
        dst = src;
        for (auto& s : dst.samples) s *= gain_;
    }

private:
    float gain_ = 1.0f;
};

class PassthroughNode : public Node {
public:
    PassthroughNode() : Node("passthrough") {}

    bool prepare(std::uint32_t, std::uint32_t) noexcept override {
        return true;
    }
    void render(std::span<const Buffer> in,
                std::span<Buffer>       out) noexcept override {
        if (in.empty() || out.empty()) return;
        out.front() = in.front();
    }
};

}  // namespace neuro::audio
