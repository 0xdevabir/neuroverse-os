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

void Scheduler::Worker::run() {
    while (!sched_.done_.load(std::memory_order_acquire)) {
        auto h = sched_.try_get(*this);
        if (h) {
            h.resume();
            continue;
        }
        // Spin: yield to scheduler and try again.
        std::this_thread::yield();
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
    global_cv_.notify_all();
}

std::size_t Scheduler::worker_count() const noexcept {
    return workers_.size();
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
    (void)self;
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