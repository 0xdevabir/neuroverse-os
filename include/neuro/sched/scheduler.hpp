// neuro/sched/scheduler.hpp
//
// Worker-pool scheduler that dispatches C++20 coroutines.
// Mirrors README §9.4. Round-robin across workers; work-stealing in Phase E.

#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace neuro::sched {

class Scheduler;

class Worker {
public:
    explicit Worker(Scheduler& s) : sched_(s) {
        thread_ = std::thread([this] { run(); });
    }
    ~Worker() { stop(); }

    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

    void post(std::coroutine_handle<> h) {
        std::lock_guard lk(mu_);
        queue_.push_back(h);
        cv_.notify_one();
    }

    void stop() {
        if (thread_.joinable()) {
            {
                std::lock_guard lk(mu_);
                done_ = true;
            }
            cv_.notify_all();
            thread_.join();
        }
    }

private:
    void run() {
        while (true) {
            std::coroutine_handle<> h;
            {
                std::unique_lock lk(mu_);
                cv_.wait(lk, [&] { return !queue_.empty() || done_; });
                if (done_ && queue_.empty()) return;
                h = queue_.front();
                queue_.pop_front();
            }
            h.resume();
        }
    }

    Scheduler&                          sched_;
    std::thread                         thread_;
    std::mutex                          mu_;
    std::condition_variable             cv_;
    std::deque<std::coroutine_handle<>> queue_;
    bool                                done_{false};
};

class Scheduler {
public:
    explicit Scheduler(std::size_t threads = std::thread::hardware_concurrency()) {
        if (threads == 0) threads = 1;
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back(std::make_unique<Worker>(*this));
        }
    }

    ~Scheduler() {
        for (auto& w : workers_) w->stop();
    }

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void post(std::coroutine_handle<> h) {
        // Round-robin for simplicity.
        auto& w = *workers_[next_++ % workers_.size()];
        w.post(h);
    }

    std::size_t worker_count() const noexcept { return workers_.size(); }

private:
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<std::size_t>            next_{0};
};

}  // namespace neuro::sched