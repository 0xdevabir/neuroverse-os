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
        std::lock_guard lk(mu_);
        queue_.push_back(std::move(m));
    }

private:
    mutable std::mutex                mu_;
    std::deque<Message>               queue_;
};

}  // namespace neuro::ipc
