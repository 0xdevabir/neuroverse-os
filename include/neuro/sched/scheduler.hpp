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

    [[nodiscard]] std::thread::id thread_id() const noexcept {
        return thread_.get_id();
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
            // Touch sched_ to keep the back-reference live; in future commits
            // the worker will use it for stealing / global queue injection.
            (void)sched_;
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

    // Batch post: enqueue a contiguous range of handles under one
    // round-robin counter advance. Each handle still receives its own
    // worker assignment; the batch path is mostly a convenience for
    // callers that want a single call site.
    template <class It>
    void post_batch(It first, It last) {
        for (auto it = first; it != last; ++it) {
            post(*it);
        }
    }

    // Overload for std::initializer_list.
    void post_batch(std::initializer_list<std::coroutine_handle<>> hs) {
        for (auto h : hs) post(h);
    }

    // Return the std::thread::id of each worker thread. Useful for
    // tests verifying that work actually lands on a specific worker.
    [[nodiscard]] std::vector<std::thread::id> worker_thread_ids() const {
        std::vector<std::thread::id> ids;
        ids.reserve(workers_.size());
        for (const auto& w : workers_) {
            // Workers store a std::thread; expose its id via a friend
            // accessor (declared in the cpp-free header by reaching
            // into the worker thread handle). Since Worker keeps
            // thread_ private, we add a thread_id() accessor below.
            ids.push_back(w->thread_id());
        }
        return ids;
    }

    std::size_t worker_count() const noexcept { return workers_.size(); }

private:
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<std::size_t>            next_{0};
};

}  // namespace neuro::sched