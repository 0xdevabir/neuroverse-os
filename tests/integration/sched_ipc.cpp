// tests/integration/sched_ipc.cpp
//
// Z7.5 — Scheduler::post on a coroutine that co_awaits an IPC send.
//
// Composes the worker-pool scheduler with the C++20 coroutine
// machinery and the IPC send_awaiter / recv_awaiter. A coroutine
// task is created, posted to the Scheduler, runs on a worker
// thread, sends messages to an Endpoint, and a separate thread
// receives them.

#include "tests/test_framework.hpp"

#include "neuro/ipc/endpoint.hpp"
#include "neuro/ipc/message.hpp"
#include "neuro/sched/scheduler.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <thread>
#include <vector>

using neuro::ipc::Endpoint;
using neuro::ipc::Message;
using neuro::ipc::Tag;
using neuro::sched::Scheduler;

namespace {

constexpr Tag SCHED_PING{0x000C, 0x0001};

// Coroutine task that suspends on initial_suspend so the Scheduler
// has something to dispatch. final_suspend returns suspend_always
// so the coroutine stays in a "done but alive" state until the
// SenderTask destructor destroys it.
struct SenderTask {
    struct promise_type {
        SenderTask get_return_object() {
            return SenderTask{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;
    };
    using handle_type = std::coroutine_handle<promise_type>;

    SenderTask(handle_type h) : handle(h) {}
    ~SenderTask() {
        if (handle) handle.destroy();
    }
    SenderTask(const SenderTask&)            = delete;
    SenderTask& operator=(const SenderTask&) = delete;
    SenderTask(SenderTask&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }
    SenderTask& operator=(SenderTask&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    handle_type handle;
};

struct SenderArgs {
    Endpoint*                       ep;
    int                             count;
    std::atomic<std::thread::id>*   worker_id;
};

SenderTask sender(SenderArgs args) {
    for (int i = 0; i < args.count; ++i) {
        std::byte b = static_cast<std::byte>(i & 0xFF);
        co_await args.ep->send(Message::bytes(
            SCHED_PING, std::vector<std::byte>{b}));
    }
    *args.worker_id = std::this_thread::get_id();
}

}  // namespace

TEST(sched_ipc, scheduler_runs_coroutine_that_sends_ipc) {
    Scheduler sched(2);
    Endpoint  ep;

    std::atomic<std::thread::id> worker_id{};
    SenderTask task = sender(SenderArgs{&ep, 5, &worker_id});
    sched.post(task.handle);

    // Drain the 5 messages on this thread.
    for (int i = 0; i < 5; ++i) {
        auto m = ep.recv_blocking(std::chrono::milliseconds{200});
        EXPECT_TRUE(m.tag == SCHED_PING);
    }

    // The coroutine completed; its worker thread id was captured.
    while (worker_id.load() == std::thread::id{}) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(worker_id.load() != std::thread::id{});
}

TEST(sched_ipc, scheduler_handles_many_concurrent_coroutines) {
    Scheduler sched(4);
    Endpoint  ep;

    constexpr int kCoros   = 16;
    constexpr int kPerCoro = 8;

    std::vector<std::unique_ptr<std::atomic<std::thread::id>>> wids;
    std::vector<SenderTask> tasks;

    for (int i = 0; i < kCoros; ++i) {
        auto wid = std::make_unique<std::atomic<std::thread::id>>();
        auto task = sender(SenderArgs{&ep, kPerCoro, wid.get()});
        auto h = task.handle;
        sched.post(h);
        tasks.push_back(std::move(task));
        wids.push_back(std::move(wid));
    }

    // Receive kCoros * kPerCoro messages.
    int got = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds{2};
    while (got < kCoros * kPerCoro &&
           std::chrono::steady_clock::now() < deadline) {
        auto m = ep.try_recv();
        if (m) {
            ++got;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    EXPECT_EQ(kCoros * kPerCoro, got);
}

RUN_ALL_TESTS()