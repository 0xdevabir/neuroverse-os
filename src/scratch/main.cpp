// src/scratch/main.cpp
//
// End-to-end demo that exercises every starter subsystem from README §9.
// Build with the Makefile in this repo, or via:
//   clang++ -std=c++20 -fcoroutines-ts -Iinclude src/scratch/main.cpp -o neuro_scratch

#include <chrono>
#include <iostream>
#include <thread>
#include <string_view>
#include <vector>
#include <coroutine>

#include "neuro/core/result.hpp"
#include "neuro/core/capability.hpp"
#include "neuro/core/endpoint.hpp"
#include "neuro/sched/scheduler.hpp"
#include "neuro/net/channel.hpp"
#include "neuro/mem/arena.hpp"

using namespace neuro;

core::Result<int> parse_int(std::string_view s) {
    if (s.empty()) {
        return std::unexpected(core::Error::make(
            core::ErrorKind::InvalidArgument, 1, "empty string"));
    }
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return std::unexpected(core::Error::make(
                core::ErrorKind::InvalidArgument, 2, "non-digit"));
        }
        v = v * 10 + (c - '0');
    }
    return v;
}

struct Job {
    std::coroutine_handle<> handle;
    int                     id;
};

sched::Scheduler g_sched;

struct Task {
    struct promise_type {
        Task get_return_object() { return Task{handle_type::from_promise(*this)}; }
        // suspend_always on initial: the coroutine waits in the worker queue.
        // suspend_never on final: the compiler auto-destroys the frame when
        // the coroutine finishes; the worker just lets the resume() return.
        // This deviates from the README wording (which uses suspend_never for
        // both, making the coroutine run synchronously and the post() a no-op),
        // but suspend_always+never composes safely with the dispatcher.
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type h;
    // Task is non-owning when final_suspend=suspend_never. The compiler
    // destroys the frame on resume past final. We just null the handle.
};

// Tasks must outlive the worker's resume call. We keep them in a global
// vector that's cleared after the scheduler has had time to drain. The vector
// itself holds the Task objects; the coroutine frames are owned by the
// compiler-generated destruction.
std::vector<Task> g_tasks;

Task demo(int id) {
    std::cout << "[task " << id << "] start on thread "
              << std::this_thread::get_id() << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "[task " << id << "] end\n";
    co_return;
}

int main() {
    if (auto r = parse_int("42"); r) {
        std::cout << "parsed = " << *r << "\n";
    } else {
        std::cout << "error: " << r.error().message << "\n";
    }

    auto cap = core::Capability::mint(0xDEADBEEF, core::CapRight::All, 1, 1);
    std::cout << "cap has Read? " << cap.has(core::CapRight::Read) << "\n";

    net::Channel<int> ch;
    std::jthread producer([&] {
        for (int i = 0; i < 5; ++i) {
            ch.send(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    std::jthread consumer([&] {
        for (int i = 0; i < 5; ++i) {
            std::cout << "recv: " << ch.recv() << "\n";
        }
    });

    g_tasks.reserve(3);
    g_tasks.emplace_back(demo(1));
    g_sched.post(g_tasks.back().h);
    g_tasks.emplace_back(demo(2));
    g_sched.post(g_tasks.back().h);
    g_tasks.emplace_back(demo(3));
    g_sched.post(g_tasks.back().h);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    g_tasks.clear(); // ~Task destroys each coroutine frame

    mem::Arena arena(1024 * 1024);
    int* xs = new (arena.allocate(sizeof(int) * 16, alignof(int))) int[16];
    for (int i = 0; i < 16; ++i) xs[i] = i * i;
    std::cout << "arena used = " << arena.used() << " bytes\n";

    return 0;
}