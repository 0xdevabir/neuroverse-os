// neuro/sched/work_stealing.hpp
//
// Work-stealing scheduler.
//
// Per README §4.5:
//   - A global injector queue (MPMC deque) so newly-posted work is
//     visible to all workers immediately.
//   - Per-worker local deques so workers can pop LIFO from the hot
//     path.
//   - steal(): a victim worker exposes its local deque head for
//     another worker to take from. The classic "work-stealing" pattern.
//
// Algorithm (Chase-Lev style, simplified for the host):
//   - Owner pop: LIFO from local (push/pop tail).
//   - Owner push: bottom of local.
//   - Steal: take from the front (oldest) of a victim's local deque.
//   - Global fallback: if both local deques are empty, drain the
//     global injector (one task at a time, MPMC).
//
// Locking: per-deque mutex is fine for the host scaffold; production
// would use atomics with epoch-based reclamation. Phase 1 replaces
// this file with the lock-free version.

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

namespace neuro::sched::ws {

class Scheduler {
public:
    explicit Scheduler(std::size_t threads = std::thread::hardware_concurrency());
    ~Scheduler();

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void post(std::coroutine_handle<> h);

    [[nodiscard]] std::size_t worker_count() const noexcept;

    // Stop all workers (also called from ~Scheduler).
    void stop_all();

    // Test helpers.
    [[nodiscard]] std::vector<std::size_t> local_sizes() const;
    [[nodiscard]] std::size_t global_size() const;

private:
    class Worker {
    public:
        Worker(Scheduler& s, std::size_t id);
        ~Worker();
        Worker(const Worker&)            = delete;
        Worker& operator=(const Worker&) = delete;

        void join();

        // LIFO from local.
        [[nodiscard]] std::coroutine_handle<> pop_local();

        // FIFO from a victim's local (oldest work moves around).
        [[nodiscard]] std::coroutine_handle<> steal();

        [[nodiscard]] std::size_t local_size() const;

    private:
        void run();

        friend class Scheduler;

        Scheduler&                          sched_;
        std::size_t                         id_;
        std::thread                         thread_;
        mutable std::mutex                  mu_;
        std::deque<std::coroutine_handle<>> local_;
    };

    [[nodiscard]] std::coroutine_handle<> try_get(Worker& self);

    std::vector<std::unique_ptr<Worker>>        workers_;
    std::deque<std::coroutine_handle<>>         global_;
    mutable std::mutex                          global_mu_;
    std::condition_variable                     global_cv_;
    std::atomic<bool>                           done_{false};
};

}  // namespace neuro::sched::ws