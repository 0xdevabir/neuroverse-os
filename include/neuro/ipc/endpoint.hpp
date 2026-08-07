// neuro/ipc/endpoint.hpp
//
// Async IPC endpoint: a typed message queue with co_await-aware send.
//
// This is the IPC primitive used by the new Phase F IPC stack.
// The earlier neuro::core::Endpoint (a bare 64-byte-payload channel
// from §9.3) is kept for the existing demo + Process wires; new
// subsystems should use neuro::ipc::Endpoint instead.
//
// Adds C++20 coroutine support for the sender:
//   - send_awaiter  — co_await until the message has been queued.
//     For the host scaffold the queue is unbounded, so we never
//     actually need to suspend; the awaiter is a thin port to the
//     kernel implementation that will park on the receiver's
//     space-available wait queue.
//
// Receive is blocking (recv_blocking / try_recv) for the host
// scaffold because we do not yet have a coroutine scheduler parked
// on per-endpoint wait queues. A future phase adds a recv_awaiter
// that integrates with neuro::sched::ws::Scheduler.

#pragma once

#include <chrono>
#include <coroutine>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include "neuro/ipc/message.hpp"

namespace neuro::ipc {

class Endpoint {
public:
    Endpoint() = default;
    Endpoint(const Endpoint&)            = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint(Endpoint&&)                 = delete;
    Endpoint& operator=(Endpoint&&)      = delete;

    // ---- Async send ---------------------------------------------------

    struct send_awaiter {
        Endpoint*   ep;
        Message     msg;

        // For the host scaffold the queue is always ready, so we
        // don't need to suspend. The kernel implementation will
        // return false here and park on the receiver's space-
        // available wait queue.
        bool await_ready() const noexcept { return true; }

        void await_suspend(std::coroutine_handle<>) const noexcept {
            // Never actually called on the host because await_ready
            // returns true; declared so the kernel swap is mechanical.
            ep->send_nowait(std::move(msg));
        }

        void await_resume() const noexcept {
            ep->send_nowait(std::move(msg));
        }
    };

    [[nodiscard]] send_awaiter send(Message m) {
        return send_awaiter{this, std::move(m)};
    }

    // ---- Async receive ------------------------------------------------

    struct recv_awaiter {
        Endpoint*              ep;
        std::optional<Message> result;
        std::coroutine_handle<> continuation{};

        // Fast path: consume an already-queued message without
        // suspending the coroutine.
        bool await_ready() {
            std::lock_guard lk(ep->mu_);
            if (ep->queue_.empty()) return false;
            result.emplace(std::move(ep->queue_.front()));
            ep->queue_.pop_front();
            return true;
        }

        // Slow path: atomically re-check the queue, then register this
        // awaiter as a receiver. send_nowait() fills `result` and resumes
        // `continuation` once a message arrives.
        bool await_suspend(std::coroutine_handle<> h) {
            std::lock_guard lk(ep->mu_);
            if (!ep->queue_.empty()) {
                result.emplace(std::move(ep->queue_.front()));
                ep->queue_.pop_front();
                return false;  // message raced with await_ready()
            }
            continuation = h;
            ep->waiters_.push_back(this);
            return true;
        }

        Message await_resume() {
            return std::move(*result);
        }
    };

    [[nodiscard]] recv_awaiter recv() {
        return recv_awaiter{this, std::nullopt, {}};
    }

    // ---- Synchronous receive (host scaffold) --------------------------

    [[nodiscard]] std::optional<Message> try_recv() {
        std::lock_guard lk(mu_);
        if (queue_.empty()) return std::nullopt;
        Message m = std::move(queue_.front());
        queue_.pop_front();
        return m;
    }

    // Block the calling thread until a message is available. Polls
    // every `poll_interval` until one arrives. Returns the message.
    Message recv_blocking(std::chrono::milliseconds poll_interval =
                              std::chrono::milliseconds{1}) {
        for (;;) {
            if (auto m = try_recv()) return std::move(*m);
            std::this_thread::sleep_for(poll_interval);
        }
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mu_);
        return queue_.size();
    }

    // ---- Internal -----------------------------------------------------

    void send_nowait(Message m) {
        std::coroutine_handle<> resume = {};
        recv_awaiter* waiter = nullptr;
        {
            std::lock_guard lk(mu_);
            if (!waiters_.empty()) {
                waiter = waiters_.front();
                waiters_.pop_front();
                resume = waiter->continuation;
                waiter->result.emplace(std::move(m));
            } else {
                queue_.push_back(std::move(m));
            }
        }
        if (resume) resume.resume();
    }

private:
    mutable std::mutex                mu_;
    std::deque<Message>               queue_;
    std::deque<recv_awaiter*>         waiters_;
};

}  // namespace neuro::ipc
