// src/sched/work_stealing.cpp
//
// Work-stealing scheduler implementation.
// Types are defined in include/neuro/sched/work_stealing.hpp.

#include "neuro/sched/work_stealing.hpp"

#include <chrono>

namespace neuro::sched::ws {

// ---- Worker implementation --------------------------------------------

Scheduler::Worker::Worker(Scheduler& s, std::size_t id)
    : sched_(s), id_(id) {
    thread_ = std::thread([this] { run(); });
}

Scheduler::Worker::~Worker() { join(); }

void Scheduler::Worker::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::coroutine_handle<> Scheduler::Worker::pop_local() {
    std::lock_guard lk(mu_);
    if (local_.empty()) return nullptr;
    auto h = local_.back();
    local_.pop_back();
    return h;
}

std::coroutine_handle<> Scheduler::Worker::steal() {
    std::lock_guard lk(mu_);
    if (local_.empty()) return nullptr;
    auto h = local_.front();
    local_.pop_front();
    return h;
}

std::size_t Scheduler::Worker::local_size() const {
    std::lock_guard lk(mu_);
    return local_.size();
}

void Scheduler::Worker::run() {
    while (!sched_.done_.load(std::memory_order_acquire)) {
        auto h = sched_.try_get(*this);
        if (h) {
            h.resume();
            continue;
        }
        // Idle: wait briefly for work or shutdown.
        std::unique_lock lk(sched_.global_mu_);
        sched_.global_cv_.wait_for(
            lk, std::chrono::milliseconds(1),
            [&] {
                return sched_.done_.load(std::memory_order_acquire) ||
                       !sched_.global_.empty();
            });
    }
}

// ---- Scheduler implementation -----------------------------------------

Scheduler::Scheduler(std::size_t threads) {
    if (threads == 0) threads = 1;
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back(std::make_unique<Worker>(*this, i));
    }
}

Scheduler::~Scheduler() { stop_all(); }

void Scheduler::post(std::coroutine_handle<> h) {
    {
        std::lock_guard lk(global_mu_);
        global_.push_back(h);
    }
    global_cv_.notify_one();
}

std::size_t Scheduler::worker_count() const noexcept {
    return workers_.size();
}

std::vector<std::size_t> Scheduler::local_sizes() const {
    std::vector<std::size_t> out;
    out.reserve(workers_.size());
    for (const auto& w : workers_) out.push_back(w->local_size());
    return out;
}

std::size_t Scheduler::global_size() const {
    std::lock_guard lk(global_mu_);
    return global_.size();
}

void Scheduler::stop_all() {
    done_.store(true, std::memory_order_release);
    global_cv_.notify_all();
    for (auto& w : workers_) w->join();
}

std::coroutine_handle<> Scheduler::try_get(Worker& self) {
    if (auto h = self.pop_local()) return h;

    // Try stealing from a sibling. Start from self.id+1 to avoid
    // always hammering the same victim.
    if (workers_.size() > 1) {
        const auto start = (self.id_ + 1) % workers_.size();
        for (std::size_t i = 0; i < workers_.size() - 1; ++i) {
            const auto idx = (start + i) % workers_.size();
            if (idx == self.id_) continue;
            if (auto h = workers_[idx]->steal()) return h;
        }
    }

    // Finally, drain the global injector.
    std::coroutine_handle<> h;
    {
        std::lock_guard lk(global_mu_);
        if (!global_.empty()) {
            h = global_.front();
            global_.pop_front();
        }
    }
    return h;
}

}  // namespace neuro::sched::ws