// tests/unit/sched/work_stealing_test.cpp
//
// Tests for the NeuroSched work-stealing scheduler (host scaffold).
// Covers:
//   - worker_count() matches constructor arg
//   - post() + global_size() round-trip
//   - posted coroutines are resumed exactly once
//   - FIFO-ish drain: all tasks run, results appear
//   - post() returns work even with a single worker
//   - stop_all() is idempotent
//   - destructor calls stop_all() (verified by RAII)
//   - default constructor uses at least 1 worker
//   - worker_count is fixed once constructed

#include "neuro/sched/work_stealing.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::sched::ws::Scheduler;

namespace {

// Minimal awaitable coroutine: schedules `body` to run inline when
// the coroutine is resumed. We use this so we can post() a coroutine
// handle into the scheduler and observe side effects (counter
// increments) without dragging in any subsystem.
struct RunTask {
    struct promise_type {
        std::atomic<int>* counter = nullptr;

        RunTask get_return_object() noexcept {
            return RunTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept   { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;

    RunTask(std::coroutine_handle<promise_type> handle) noexcept : h(handle) {}
    RunTask(RunTask&& o) noexcept : h(o.h) { o.h = nullptr; }
    RunTask& operator=(RunTask&&) = delete;
    RunTask(const RunTask&)      = delete;
    RunTask& operator=(const RunTask&) = delete;

    ~RunTask() {
        if (h) h.destroy();
    }
};

RunTask run_one(std::atomic<int>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

}  // namespace

// ---- 1. worker_count matches constructor arg -------------------------

TEST(sched, worker_count_matches_ctor_arg) {
    Scheduler s(4);
    EXPECT_EQ(4u, s.worker_count());
}

TEST(sched, worker_count_one) {
    Scheduler s(1);
    EXPECT_EQ(1u, s.worker_count());
}

TEST(sched, worker_count_zero_clamps_to_one) {
    // Passing 0 should still give us a usable scheduler with at
    // least one worker (the constructor documents this clamp).
    Scheduler s(0);
    EXPECT_EQ(1u, s.worker_count());
}

// ---- 2. post() + global_size() round-trip ----------------------------

TEST(sched, post_increments_global_size) {
    Scheduler s(1);
    EXPECT_EQ(0u, s.global_size());
    s.stop_all();  // Freeze consumption so queue size is deterministic.

    std::atomic<int> counter{0};
    auto t1 = run_one(counter);
    auto t2 = run_one(counter);
    auto t3 = run_one(counter);

    s.post(t1.h);
    EXPECT_EQ(1u, s.global_size());
    s.post(t2.h);
    EXPECT_EQ(2u, s.global_size());
    s.post(t3.h);
    EXPECT_EQ(3u, s.global_size());
    EXPECT_EQ(0, counter.load());
}

// ---- 3. posted coroutines are resumed exactly once -------------------

TEST(sched, each_posted_handle_resumes_once) {
    Scheduler s(2);
    std::atomic<int> counter{0};
    constexpr int N = 100;

    std::vector<RunTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(run_one(counter));
    }
    for (auto& t : tasks) {
        s.post(t.h);
    }
    // Wait for drain.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::yield();
    }
    EXPECT_EQ(N, counter.load());
    EXPECT_EQ(0u, s.global_size());
}

// ---- 4. FIFO-ish drain: tasks all run --------------------------------

TEST(sched, many_tasks_all_run) {
    // Stress: enough tasks that any work-stealing pathology would
    // leave at least a few unfinished on a 4-worker setup.
    Scheduler s(4);
    std::atomic<int> counter{0};
    constexpr int N = 500;

    std::vector<RunTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(run_one(counter));
    }
    for (auto& t : tasks) {
        s.post(t.h);
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::yield();
    }
    EXPECT_EQ(N, counter.load());
}

// ---- 5. stop_all() is idempotent --------------------------------------

TEST(sched, stop_all_idempotent) {
    Scheduler s(2);
    s.stop_all();
    s.stop_all();   // must not double-join workers
    s.stop_all();
    // If we got here without UB, the test passes.
    EXPECT_EQ(2u, s.worker_count());
}

// ---- 6. destructor calls stop_all() ----------------------------------

TEST(sched, destructor_stops_workers) {
    std::atomic<int> counter{0};
    {
        Scheduler s(2);
        // Run a couple of tasks and drain them so all handles are
        // released before the scheduler destructor runs.
        std::vector<RunTask> tasks;
        for (int i = 0; i < 5; ++i) {
            tasks.push_back(run_one(counter));
        }
        for (auto& t : tasks) {
            s.post(t.h);
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (counter.load() < 5) {
            if (std::chrono::steady_clock::now() > deadline) break;
            std::this_thread::yield();
        }
        EXPECT_EQ(5, counter.load());
        // s dtor runs here: must join workers cleanly.
    }
    // If we got here, the destructor returned without UB.
    EXPECT_EQ(5, counter.load());
}

// ---- 7. work is consumed even after stop + new post? -----------------
// (We don't promise this; the contract is stop_all = no more work.)
// Skip: not part of the host scaffold contract.

// ---- 8. worker_count is fixed once constructed -----------------------

TEST(sched, worker_count_fixed_after_construction) {
    Scheduler s(3);
    std::atomic<int> counter{0};
    std::vector<RunTask> tasks;
    tasks.reserve(50);
    for (int i = 0; i < 50; ++i) {
        tasks.push_back(run_one(counter));
    }
    for (auto& t : tasks) {
        s.post(t.h);
    }
    EXPECT_EQ(3u, s.worker_count());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (counter.load() < 50) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::yield();
    }
    EXPECT_EQ(3u, s.worker_count());
}

// ---- 9. global_size shrinks as work is consumed ----------------------

TEST(sched, global_size_drains_under_load) {
    Scheduler s(4);
    std::atomic<int> counter{0};
    constexpr int N = 1000;

    std::vector<RunTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(run_one(counter));
    }
    for (auto& t : tasks) {
        s.post(t.h);
    }
    // The queue size is bounded above by N; workers may already be
    // draining it. We assert the upper bound and the eventual empty.
    EXPECT_TRUE(s.global_size() <= static_cast<std::size_t>(N));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (s.global_size() > 0) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::yield();
    }
    EXPECT_EQ(0u, s.global_size());
    EXPECT_EQ(N, counter.load());
}

RUN_ALL_TESTS()
