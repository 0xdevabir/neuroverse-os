// src/proc/thread.cpp
//
// Implementation of neuro::proc::Thread.
//
// The host scaffold runs the user entry on a std::thread. State
// transitions are guarded by mu_ + cv_; state_ is also atomic for
// read-only inspection.

#include "neuro/proc/thread.hpp"

#include <utility>

namespace neuro::proc {

Thread::Thread(Process& owner, Attr a, Entry fn)
    : core::KObject(core::KObjectKind::Thread),
      attr_(std::move(a)),
      owner_(&owner),
      entry_(std::move(fn)) {}

Thread::~Thread() {
    // If the caller forgot to join or detach, make sure the OS thread
    // is no longer running. join() is idempotent: re-joining a finished
    // thread is well-defined.
    join();
}

void Thread::start() {
    // std::thread is not movable once constructed; we std::thread it
    // here so copy/move of Thread itself stays simple.
    auto expected = ThreadState::Ready;
    if (!state_.compare_exchange_strong(expected, ThreadState::Running)) {
        return; // already started or terminated
    }
    os_thread_ = std::thread([this] { run_loop(); });
}

void Thread::join() {
    if (os_thread_.joinable()) {
        os_thread_.join();
        state_.store(ThreadState::Terminated, std::memory_order_release);
    }
}

bool Thread::join_for(std::chrono::milliseconds d) {
    // std::thread doesn't have timeout join; run a small helper that
    // signals via a future.
    if (!os_thread_.joinable()) return true;
    std::thread helper([this] {
        if (os_thread_.joinable()) os_thread_.join();
    });
    // Polling join is acceptable for the host stub; the kernel version
    // would simply block on the thread's completion.
    auto deadline = std::chrono::steady_clock::now() + d;
    while (!done_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done_.load(std::memory_order_acquire);
}

void Thread::wake() noexcept {
    {
        std::lock_guard lk(mu_);
        if (state_.load() == ThreadState::Waiting) {
            state_.store(ThreadState::Ready, std::memory_order_release);
        }
    }
    cv_.notify_one();
}

ThreadState Thread::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

neuro::sec::CapabilitySpace& Thread::caps() noexcept {
    return owner_->caps();
}

void Thread::run_loop() {
    try {
        entry_(*this);
    } catch (...) {
        // Unhandled exceptions escalate to the process; the host
        // scaffold simply rethrows to std::terminate as the README's
        // Task does.
        std::terminate();
    }
    {
        std::lock_guard lk(mu_);
        state_.store(ThreadState::Zombie, std::memory_order_release);
        done_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

}  // namespace neuro::proc