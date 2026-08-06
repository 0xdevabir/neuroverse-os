// tests/integration/sched_steal.cpp
//
// Work-stealing scheduler test.
//
// Spawns 32 trivial coroutine tasks across 2 workers and verifies:
//   1. All tasks run (visited == 32).
//   2. The work fans out across both worker OS threads (unique_threads >= 2).
//   3. The global injector empties (global_size == 0) once workers drain.
//
// Not deterministic which worker runs which task; we only check that
// the load-balancing is real.

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/sched/work_stealing.hpp"

using neuro::sched::ws::Scheduler;

namespace {

struct Task {
    struct promise_type {
        Task get_return_object() { return Task{handle_type::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never  final_suspend()   noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;
    };
    using handle_type = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(handle_type h_) noexcept : h(h_) {}

    // The handle is owned by *one* owner: whoever first resumes it.
    // After resume() the coroutine runs to final_suspend, and because
    // final_suspend returns suspend_never the compiler auto-destroys
    // the frame. So a Task whose coroutine has already been resumed
    // holds a *dangling* handle; we must NOT destroy it again.
    ~Task() = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : h(other.h) { other.h = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }

    handle_type h = nullptr;
};

std::atomic<int> g_visited{0};
std::mutex       g_seen_mu;
std::vector<std::thread::id> g_seen;

Task trivial(int id) {
    g_visited.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lk(g_seen_mu);
        g_seen.push_back(std::this_thread::get_id());
    }
    (void)id;
    // Yield briefly so the second worker thread has a chance to enter its
    // run loop and pick up later tasks. Without this, 32 trivial coroutines
    // can be drained by a single fast worker before the other wakes up.
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    co_return;
}

}  // namespace

TEST(steal, all_tasks_run_and_fan_out) {
    g_visited = 0;
    {
        std::lock_guard lk(g_seen_mu);
        g_seen.clear();
    }

    Scheduler s(/*threads=*/2);
    std::vector<Task> tasks;
    tasks.reserve(32);
    for (int i = 0; i < 32; ++i) {
        tasks.emplace_back(trivial(i));
        s.post(tasks.back().h);
    }

    // Wait for completion. There's no built-in barrier in the host
    // stub, so we sleep briefly.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(g_visited.load(), 32);

    std::set<std::thread::id> uniq;
    {
        std::lock_guard lk(g_seen_mu);
        uniq.insert(g_seen.begin(), g_seen.end());
    }
    // With work stealing, we expect both worker threads to have run
    // at least one task each.
    EXPECT_TRUE(uniq.size() >= 2u);

    // Global injector should be drained.
    EXPECT_EQ(s.global_size(), 0u);

    // Stop workers.
    s.stop_all();
}

TEST(steal, single_worker_serializes_tasks) {
    g_visited = 0;
    {
        std::lock_guard lk(g_seen_mu);
        g_seen.clear();
    }

    Scheduler s(/*threads=*/1);
    std::vector<Task> tasks;
    tasks.reserve(8);
    for (int i = 0; i < 8; ++i) {
        tasks.emplace_back(trivial(i));
        s.post(tasks.back().h);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    EXPECT_EQ(g_visited.load(), 8);

    std::set<std::thread::id> uniq;
    {
        std::lock_guard lk(g_seen_mu);
        uniq.insert(g_seen.begin(), g_seen.end());
    }
    EXPECT_EQ(uniq.size(), 1u);

    s.stop_all();
}

TEST(steal, post_is_thread_safe) {
    // Many threads post concurrently; the global injector must accept
    // every post without losing any.
    g_visited = 0;
    Scheduler s(/*threads=*/2);
    constexpr int kPostsPerThread = 100;
    constexpr int kThreads        = 4;
    std::atomic<int> posted{0};

    std::vector<std::thread> posters;
    std::vector<Task> tasks;
    tasks.reserve(kPostsPerThread * kThreads);
    std::mutex tasks_mu;

    for (int t = 0; t < kThreads; ++t) {
        posters.emplace_back([&] {
            for (int i = 0; i < kPostsPerThread; ++i) {
                Task task = trivial(i);
                std::coroutine_handle<> to_post;
                {
                    std::lock_guard lk(tasks_mu);
                    tasks.push_back(std::move(task));
                    to_post = tasks.back().h;
                }
                s.post(to_post);
                posted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : posters) th.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(posted.load(),         kPostsPerThread * kThreads);
    EXPECT_EQ(g_visited.load(),      kPostsPerThread * kThreads);
    EXPECT_EQ(s.global_size(),       0u);

    s.stop_all();
}

RUN_ALL_TESTS()