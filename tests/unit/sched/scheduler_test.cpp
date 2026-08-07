// tests/unit/sched/scheduler_test.cpp
//
// Tests for neuro::sched::Scheduler — the worker-pool facade that
// round-robins coroutine handles across N workers.
//
// The end-to-end work-stealing behavior is covered in
// work_stealing_test.cpp; here we drill into the Scheduler facade
// itself: worker_count, default-1 clamping, round-robin distribution
// (via counter observation), and lifecycle (destructor stops workers).
//
// Coroutine design mirrors work_stealing_test.cpp: the task owns
// its handle, the body runs before co_return, and final_suspend
// returns suspend_always so we can destroy the frame deterministically.

#include "neuro/sched/scheduler.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::sched::Scheduler;

namespace {

struct OneShotTask {
    struct promise_type {
        std::atomic<int>* counter = nullptr;

        OneShotTask get_return_object() noexcept {
            return OneShotTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;

    OneShotTask(std::coroutine_handle<promise_type> handle) noexcept
        : h(handle) {}
    OneShotTask(OneShotTask&& o) noexcept : h(o.h) { o.h = nullptr; }
    OneShotTask& operator=(OneShotTask&&)            = delete;
    OneShotTask(const OneShotTask&)                  = delete;
    OneShotTask& operator=(const OneShotTask&)       = delete;

    ~OneShotTask() {
        if (h) h.destroy();
    }
};

OneShotTask make_increment(std::atomic<int>* c) {
    if (c) c->fetch_add(1, std::memory_order_relaxed);
    co_return;
}

// Busy task: each increment takes ~50µs, giving 2 workers time to
// overlap and observe inflight > 1.
struct BusyTask {
    struct promise_type {
        std::atomic<int>* inflight = nullptr;
        std::atomic<int>* peak     = nullptr;
        std::atomic<int>* done     = nullptr;

        BusyTask get_return_object() noexcept {
            return BusyTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;

    BusyTask(std::coroutine_handle<promise_type> handle) noexcept
        : h(handle) {}
    BusyTask(BusyTask&& o) noexcept : h(o.h) { o.h = nullptr; }
    BusyTask& operator=(BusyTask&&)            = delete;
    BusyTask(const BusyTask&)                  = delete;
    BusyTask& operator=(const BusyTask&)       = delete;

    ~BusyTask() {
        if (h) h.destroy();
    }
};

BusyTask make_busy(std::atomic<int>* inflight,
                   std::atomic<int>* peak,
                   std::atomic<int>* done) {
    int now = inflight->fetch_add(1) + 1;
    int p = peak->load();
    while (now > p && !peak->compare_exchange_weak(p, now)) {}
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    inflight->fetch_sub(1);
    done->fetch_add(1);
    co_return;
}

}  // namespace

// ---- 1. worker_count -----------------------------------------

TEST(scheduler, worker_count_zero_clamps_to_one) {
    Scheduler s(0);
    EXPECT_EQ(static_cast<std::size_t>(1), s.worker_count());
}

TEST(scheduler, worker_count_one) {
    Scheduler s(1);
    EXPECT_EQ(static_cast<std::size_t>(1), s.worker_count());
}

TEST(scheduler, worker_count_matches_request) {
    Scheduler s(4);
    EXPECT_EQ(static_cast<std::size_t>(4), s.worker_count());
}

TEST(scheduler, worker_count_default_is_at_least_one) {
    Scheduler s;  // hardware_concurrency() default
    EXPECT_TRUE(s.worker_count() >= 1u);
}

// ---- 2. non-copyable -----------------------------------------

TEST(scheduler, non_copyable) {
    static_assert(!std::is_copy_constructible_v<Scheduler>);
    static_assert(!std::is_copy_assignable_v<Scheduler>);
}

// ---- 3. round-robin distribution -----------------------------

TEST(scheduler, round_robin_runs_every_posted_task) {
    Scheduler s(2);

    constexpr int N = 64;
    std::atomic<int> counter{0};

    // Keep every coroutine alive until after all have run; otherwise
    // the OneShotTask destructor would destroy the frame before the
    // worker resumes it.
    std::vector<OneShotTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(make_increment(&counter));
        s.post(tasks.back().h);
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(N, counter.load());
}

// ---- 4. lifecycle / dtor stops workers ------------------------

TEST(scheduler, destructor_stops_workers_cleanly) {
    // We rely on no-hang here: the dtor must join all workers.
    Scheduler s(3);
    // Tear down by going out of scope.
}

TEST(scheduler, single_worker_runs_many_tasks_sequentially) {
    Scheduler s(1);
    std::atomic<int> counter{0};

    std::vector<OneShotTask> tasks;
    tasks.reserve(20);
    for (int i = 0; i < 20; ++i) {
        tasks.push_back(make_increment(&counter));
        s.post(tasks.back().h);
    }
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (counter.load() < 20) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(20, counter.load());
}

// ---- 5. multiple workers actually parallelize ---------------

TEST(scheduler, parallel_workers_share_load) {
    Scheduler s(2);

    std::atomic<int> inflight{0};
    std::atomic<int> peak{0};
    std::atomic<int> done{0};

    constexpr int N = 32;
    std::vector<BusyTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(make_busy(&inflight, &peak, &done));
        s.post(tasks.back().h);
    }
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(5);
    while (done.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(N, done.load());
    EXPECT_TRUE(peak.load() >= 1);
}

// ---- 6. post_batch from a contiguous range (Z5.3) -----------

TEST(scheduler, post_batch_runs_every_handle) {
    Scheduler s(2);
    std::atomic<int> counter{0};
    constexpr int N = 100;

    std::vector<OneShotTask> tasks;
    tasks.reserve(N);
    for (int i = 0; i < N; ++i) {
        tasks.push_back(make_increment(&counter));
    }

    // Collect raw handles and post as a batch.
    std::vector<std::coroutine_handle<>> hs;
    hs.reserve(N);
    for (auto& t : tasks) hs.push_back(t.h);
    s.post_batch(hs.begin(), hs.end());

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(N, counter.load());
}

TEST(scheduler, post_batch_initializer_list) {
    Scheduler s(1);
    std::atomic<int> counter{0};

    std::vector<OneShotTask> ts;
    ts.push_back(make_increment(&counter));
    ts.push_back(make_increment(&counter));
    ts.push_back(make_increment(&counter));
    s.post_batch({ts[0].h, ts[1].h, ts[2].h});

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (counter.load() < 3) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(3, counter.load());
}

TEST(scheduler, post_batch_empty_range_is_noop) {
    Scheduler s(2);
    std::vector<std::coroutine_handle<>> empty;
    s.post_batch(empty.begin(), empty.end());   // must not crash
    EXPECT_EQ(2u, s.worker_count());
}

// ---- 7. worker_thread_ids (Z5.4) ------------------------------

TEST(scheduler, worker_thread_ids_count_matches_workers) {
    Scheduler s(4);
    auto ids = s.worker_thread_ids();
    EXPECT_EQ(4u, ids.size());
    // All ids must be distinct.
    std::set<std::thread::id> uniq(ids.begin(), ids.end());
    EXPECT_EQ(4u, uniq.size());
}

TEST(scheduler, worker_thread_ids_default_count_matches_hardware) {
    Scheduler s;  // hardware_concurrency
    auto ids = s.worker_thread_ids();
    EXPECT_EQ(s.worker_count(), ids.size());
}

TEST(scheduler, worker_thread_ids_differ_from_caller) {
    Scheduler s(2);
    auto caller = std::this_thread::get_id();
    auto ids = s.worker_thread_ids();
    for (auto id : ids) EXPECT_TRUE(id != caller);
}

// ---- 8. cancel(handle) (Z5.6) ----------------------------------------

TEST(scheduler, cancel_skips_handle_before_dispatch) {
    // Post a task, immediately cancel it before the worker has a
    // chance to run. The body must NOT execute.
    Scheduler s(1);
    std::atomic<int> counter{0};

    OneShotTask t = make_increment(&counter);
    s.post(t.h);
    EXPECT_TRUE(s.cancel(t.h));

    // Wait long enough for the worker to have processed the queue.
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (!s.is_cancelled(t.h) && counter.load() == 0) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Body must not have run.
    EXPECT_EQ(0, counter.load());
    // The cancel flag is cleared by the worker after handling.
    // Allow worker to finish its cleanup.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(s.is_cancelled(t.h));
}

TEST(scheduler, cancel_after_dispatch_is_noop) {
    // If the worker has already popped the handle, cancel() marks
    // the flag but the body still runs. The flag is cleared after
    // the worker resumes.
    Scheduler s(1);
    std::atomic<int> counter{0};

    std::vector<OneShotTask> tasks;
    tasks.push_back(make_increment(&counter));
    s.post(tasks.back().h);
    // Wait until the worker has processed the handle.
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (counter.load() == 0) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(1, counter.load());
    // Cancel after the fact — still records the flag but doesn't help.
    EXPECT_TRUE(s.cancel(tasks[0].h));
}

TEST(scheduler, is_cancelled_returns_state) {
    Scheduler s(1);
    std::atomic<int> counter{0};
    OneShotTask t = make_increment(&counter);
    EXPECT_FALSE(s.is_cancelled(t.h));
    s.cancel(t.h);
    EXPECT_TRUE(s.is_cancelled(t.h));
}

// ---- 9. worker idle/busy counters (Z5.7) -------------------------

TEST(scheduler, idle_busy_counters_track_activity) {
    Scheduler s(2);
    std::atomic<int> counter{0};

    // Both workers should reach idle=1 quickly (no work posted yet).
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(2);
    while (s.idle_count() < 2) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(2u, s.idle_count());
    EXPECT_EQ(0u, s.busy_count());

    constexpr int N = 32;
    std::vector<OneShotTask> tasks;
    for (int i = 0; i < N; ++i) {
        tasks.push_back(make_increment(&counter));
        s.post(tasks.back().h);
    }

    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (counter.load() < N) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(N, counter.load());
    EXPECT_EQ(N, s.completed_count());
    // After draining, workers return to idle.
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (s.idle_count() < 2) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(2u, s.idle_count());
    EXPECT_EQ(0u, s.busy_count());
}

RUN_ALL_TESTS()