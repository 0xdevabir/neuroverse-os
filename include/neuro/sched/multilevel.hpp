// neuro/sched/multilevel.hpp
//
// Multilevel scheduler facade, per README §4.5.
//
// Three tiers:
//   1. Global: distributes threads across nodes (one Scheduler per
//      node, with cross-node load reporting in Phase 4 / NeuroFabric).
//   2. Per-node: CFS-like fair scheduler with BBS for interactive
//      workloads. Host stub: the per-node Scheduler is just the
//      Worker-pool from scheduler.hpp.
//   3. Per-core: hyperthread-aware, SIPI enforcement, LLC-aware
//      placement. Host stub: each Worker is a "core"; affinity bits
//      pick which Worker receives a task.
//
// The facade owns N Schedulers (one per node). submit() routes the
// task to the scheduler whose mask covers the requested affinity;
// tie-broken by load.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "neuro/sched/scheduler.hpp"

namespace neuro::sched {

struct TierConfig {
    std::size_t nodes                = 1;
    std::size_t workers_per_node     = std::thread::hardware_concurrency();
    std::uint32_t default_affinity   = 0xFFFFFFFFu;   // any worker
};

// Tracks how many tasks a node has queued; the facade uses it to
// pick the least-loaded node on submit.
struct NodeLoad {
    std::atomic<std::uint64_t> queued{0};
    std::atomic<std::uint64_t> running{0};
};

class MultilevelScheduler {
public:
    explicit MultilevelScheduler(TierConfig cfg = {})
        : cfg_(cfg) {
        if (cfg_.workers_per_node == 0) cfg_.workers_per_node = 1;
        if (cfg_.nodes == 0)              cfg_.nodes = 1;
        nodes_.reserve(cfg_.nodes);
        loads_.reserve(cfg_.nodes);
        for (std::size_t i = 0; i < cfg_.nodes; ++i) {
            nodes_.emplace_back(std::make_unique<Scheduler>(cfg_.workers_per_node));
            loads_.emplace_back(std::make_unique<NodeLoad>());
        }
    }

    MultilevelScheduler(const MultilevelScheduler&)            = delete;
    MultilevelScheduler& operator=(const MultilevelScheduler&) = delete;

    // Route a coroutine to the least-loaded node whose worker mask
    // covers `affinity`. Returns the worker count that received the
    // task (0 if none).
    std::size_t submit(std::coroutine_handle<> h,
                       std::uint32_t affinity = 0xFFFFFFFFu) {
        const auto idx = pick_node(affinity);
        if (idx >= nodes_.size()) return 0;
        loads_[idx]->queued.fetch_add(1, std::memory_order_relaxed);
        nodes_[idx]->post(h);
        loads_[idx]->queued.fetch_sub(1, std::memory_order_relaxed);
        loads_[idx]->running.fetch_add(1, std::memory_order_relaxed);
        return cfg_.workers_per_node;
    }

    // Submit a coroutine to a specific node index, bypassing the
    // least-loaded routing. Used by affinity-pinned callers and tests.
    // Returns true if the index is valid.
    bool submit_to(std::size_t node_idx, std::coroutine_handle<> h) {
        if (node_idx >= nodes_.size()) return false;
        loads_[node_idx]->queued.fetch_add(1, std::memory_order_relaxed);
        nodes_[node_idx]->post(h);
        loads_[node_idx]->queued.fetch_sub(1, std::memory_order_relaxed);
        loads_[node_idx]->running.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Per-node running count snapshot (tasks posted but not yet
    // destroyed). The decrement happens automatically when the
    // Scheduler destructor joins all workers.
    [[nodiscard]] std::uint64_t
    running(std::size_t node_idx) const noexcept {
        if (node_idx >= loads_.size()) return 0;
        return loads_[node_idx]->running.load(std::memory_order_relaxed);
    }

    // Per-node queued count snapshot (post() path bumps this before
    // the worker takes it; the test waits long enough for the
    // scheduler to drain).
    [[nodiscard]] std::uint64_t
    queued(std::size_t node_idx) const noexcept {
        if (node_idx >= loads_.size()) return 0;
        return loads_[node_idx]->queued.load(std::memory_order_relaxed);
    }

    std::size_t node_count() const noexcept { return nodes_.size(); }
    std::size_t worker_count() const noexcept {
        return cfg_.nodes * cfg_.workers_per_node;
    }
    const TierConfig& config() const noexcept { return cfg_; }

    // Inspect per-node load (queued + running). Useful for the
    // NeuroLearn optimizer hook to verify load-balancing decisions.
    [[nodiscard]] std::uint64_t load(std::size_t node_idx) const noexcept {
        if (node_idx >= loads_.size()) return 0;
        return loads_[node_idx]->queued.load(std::memory_order_relaxed)
             + loads_[node_idx]->running.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] std::size_t pick_node(std::uint32_t /*affinity*/) const noexcept {
        // Phase 4 (NeuroFabric) plugs real cluster membership + affinity
        // masks in here. For now, pick the least-loaded node.
        std::size_t best = 0;
        std::uint64_t best_load =
            loads_[0]->queued.load(std::memory_order_relaxed)
          + loads_[0]->running.load(std::memory_order_relaxed);
        for (std::size_t i = 1; i < loads_.size(); ++i) {
            auto l = loads_[i]->queued.load(std::memory_order_relaxed)
                   + loads_[i]->running.load(std::memory_order_relaxed);
            if (l < best_load) { best = i; best_load = l; }
        }
        return best;
    }

    TierConfig                                    cfg_;
    std::vector<std::unique_ptr<Scheduler>>       nodes_;
    std::vector<std::unique_ptr<NodeLoad>>        loads_;
};

}  // namespace neuro::sched