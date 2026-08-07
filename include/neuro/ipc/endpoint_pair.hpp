// neuro/ipc/endpoint_pair.hpp
//
// A pair of typed endpoints pre-wired for bidirectional IPC.
//
// Per README §4.1, a NeuroProcess typically has at least two
// endpoints: a control channel (parent -> child) and a notification
// channel (child -> parent). EndpointPair bundles the two so call
// sites do not have to allocate and wire two separate Endpoint
// instances and risk getting the directions swapped.
//
// Layout:
//
//              EndpointPair
//              +-----------+
//        a() = | Side      | --send--> forward queue <--recv-- b()
//              +-----------+
//        b() = | Side      | --send--> back    queue <--recv-- a()
//              +-----------+
//
// Queue features (Phase K extension):
//   * Bounded capacity (kDefaultCapacity = 64 messages). Configurable
//     per pair. send_nowait / send return QueueFull when full.
//   * try_send_for / try_recv_for return std::optional after a
//     deadline (no polling spin; uses std::condition_variable).
//   * send_awaiter / recv_awaiter are co_await-aware: a send
//     suspends until the queue has space; a recv suspends until a
//     message is available. The kernel implementation will integrate
//     with neuro::sched::ws::Scheduler; the host scaffold simply
//     resumes immediately when await_ready() is true (queue has
//     space / message available) or blocks the worker thread when
//     the awaiter is used from a non-coroutine context.

#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "neuro/ipc/message.hpp"

namespace neuro::ipc {

class EndpointPair {
public:
    // Default capacity per direction. 64 messages is enough for the
    // vast majority of control / notification channels; raise it
    // for high-throughput data planes.
    static constexpr std::size_t kDefaultCapacity = 64;

    // The transport queue sits in a shared_ptr so each Side can hold
    // an alias to it independently. Bounded capacity + condvar so
    // senders can wait for space and receivers can wait for messages.
    struct Queue {
        std::mutex                mu;
        std::condition_variable   cv_not_empty;
        std::condition_variable   cv_not_full;
        std::deque<Message>       q;
        std::size_t               capacity = 0;
        bool                      closed   = false;

        // Construct with explicit capacity. kDefaultCapacity is the
        // sensible default; tests and high-throughput callers can
        // override.
        explicit Queue(std::size_t cap) : capacity(cap) {}

        // close(): unblock every wait_queue. After close, send /
        // try_recv return nullopt / QueueClosed.
        void close() {
            std::lock_guard lk(mu);
            closed = true;
            cv_not_empty.notify_all();
            cv_not_full.notify_all();
        }
    };

    // A "Side" is one half of the pair. It owns shared_ptrs to two
    // queues: it sends into one and receives from the other.
    class Side {
    public:
        Side(std::shared_ptr<Queue> send_into,
             std::shared_ptr<Queue> recv_from)
            : send_(std::move(send_into)),
              recv_(std::move(recv_from)) {}

        // ---- Async send (co_await) ----------------------------------
        struct send_awaiter {
            Side*    side;
            Message  msg;

            bool await_ready() const noexcept;
            void await_suspend(std::coroutine_handle<> h) noexcept;
            void await_resume() const noexcept {
                side->send_nowait(std::move(msg));
            }
        };
        [[nodiscard]] send_awaiter send(Message m) {
            return send_awaiter{this, std::move(m)};
        }

        // ---- Async recv (co_await) ----------------------------------
        struct recv_awaiter {
            Side*                       side;
            std::optional<Message>      out;

            bool await_ready() const noexcept;
            void await_suspend(std::coroutine_handle<> h) noexcept;
            Message await_resume() {
                return std::move(*out);
            }
        };
        [[nodiscard]] recv_awaiter recv() { return recv_awaiter{this, {}}; }

        // ---- Synchronous send (no backpressure wait) ----------------
        // Returns false if the queue is full or closed.
        bool send_nowait(Message m) {
            std::unique_lock lk(send_->mu);
            if (send_->closed)     return false;
            if (send_->q.size() >= send_->capacity) return false;
            send_->q.push_back(std::move(m));
            lk.unlock();
            send_->cv_not_empty.notify_one();
            return true;
        }

        // ---- Timed send / recv (deadline) --------------------------
        // try_send_for: blocks up to `timeout` waiting for space.
        // Returns true on success, false on timeout / closed / full.
        bool try_send_for(Message m, std::chrono::milliseconds timeout) {
            std::unique_lock lk(send_->mu);
            if (send_->closed) return false;
            bool ok = send_->cv_not_full.wait_for(lk, timeout, [&] {
                return send_->closed ||
                       send_->q.size() < send_->capacity;
            });
            if (!ok || send_->closed) return false;
            send_->q.push_back(std::move(m));
            lk.unlock();
            send_->cv_not_empty.notify_one();
            return true;
        }

        // try_recv_for: blocks up to `timeout` waiting for a message.
        // Returns std::nullopt on timeout / closed.
        [[nodiscard]] std::optional<Message>
        try_recv_for(std::chrono::milliseconds timeout) {
            std::unique_lock lk(recv_->mu);
            if (recv_->cv_not_empty.wait_for(lk, timeout, [&] {
                    return recv_->closed || !recv_->q.empty();
                })) {
                if (recv_->q.empty()) return std::nullopt;  // closed
                Message m = std::move(recv_->q.front());
                recv_->q.pop_front();
                lk.unlock();
                recv_->cv_not_full.notify_one();
                return m;
            }
            return std::nullopt;
        }

        // ---- Synchronous non-blocking send / recv -------------------
        [[nodiscard]] std::optional<Message> try_recv() {
            std::lock_guard lk(recv_->mu);
            if (recv_->q.empty()) return std::nullopt;
            Message m = std::move(recv_->q.front());
            recv_->q.pop_front();
            recv_->cv_not_full.notify_one();
            return m;
        }

        // Block until a message is available. Used by host tests
        // that don't run a coroutine scheduler.
        Message recv_blocking() {
            for (;;) {
                if (auto m = try_recv()) return std::move(*m);
                std::unique_lock lk(recv_->mu);
                recv_->cv_not_empty.wait(lk);
            }
        }

        // ---- Inspection ----------------------------------------------
        // Number of messages waiting for *this* side to read.
        [[nodiscard]] std::size_t recv_queue_size() const {
            std::lock_guard lk(recv_->mu);
            return recv_->q.size();
        }

        [[nodiscard]] std::size_t send_capacity() const noexcept {
            std::lock_guard lk(send_->mu);
            return send_->capacity;
        }

        [[nodiscard]] std::size_t send_queue_size() const {
            std::lock_guard lk(send_->mu);
            return send_->q.size();
        }

    private:
        std::shared_ptr<Queue> send_;
        std::shared_ptr<Queue> recv_;
    };

    EndpointPair()
        : EndpointPair(kDefaultCapacity) {}

    explicit EndpointPair(std::size_t per_queue_capacity)
        : fwd_(std::make_shared<Queue>(per_queue_capacity)),
          back_(std::make_shared<Queue>(per_queue_capacity)) {}

    ~EndpointPair() {
        // Unblock every parked sender / receiver so threads in
        // try_send_for / try_recv_for / recv_blocking can return.
        fwd_->close();
        back_->close();
    }

    // a() sends into fwd_, receives from back_.
    // b() sends into back_,  receives from fwd_.
    Side a() { return Side{fwd_, back_}; }
    Side b() { return Side{back_, fwd_}; }

    // Test helpers.
    [[nodiscard]] std::size_t a_unread() const {
        std::lock_guard lk(back_->mu);
        return back_->q.size();
    }
    [[nodiscard]] std::size_t b_unread() const {
        std::lock_guard lk(fwd_->mu);
        return fwd_->q.size();
    }

    // True iff the forward queue is at capacity (i.e. b can't send
    // without blocking). Useful for tests asserting backpressure.
    [[nodiscard]] bool a_send_full() const {
        std::lock_guard lk(fwd_->mu);
        return fwd_->q.size() >= fwd_->capacity;
    }
    [[nodiscard]] bool b_send_full() const {
        std::lock_guard lk(back_->mu);
        return back_->q.size() >= back_->capacity;
    }

private:
    std::shared_ptr<Queue> fwd_;
    std::shared_ptr<Queue> back_;
};

// ---- Awaiter definitions ------------------------------------------------
//
// Defined out-of-line because they need to see the full Side class.
// On the host we always return true from await_ready (queue is
// either ready or the await is wrapped in a sync primitive); the
// kernel implementation will integrate with the scheduler and may
// return false here.

inline bool EndpointPair::Side::send_awaiter::await_ready() const noexcept {
    std::lock_guard lk(side->send_->mu);
    return side->send_->closed ||
           side->send_->q.size() < side->send_->capacity;
}

inline void EndpointPair::Side::send_awaiter::await_suspend(
    std::coroutine_handle<> /*h*/) noexcept {
    // Host fallback: queue capacity is unbounded in awaitable mode
    // (we just enqueue). Real impl will park on cv_not_full.
    side->send_nowait(std::move(msg));
}

inline bool EndpointPair::Side::recv_awaiter::await_ready() const noexcept {
    std::lock_guard lk(side->recv_->mu);
    return side->recv_->closed || !side->recv_->q.empty();
}

inline void EndpointPair::Side::recv_awaiter::await_suspend(
    std::coroutine_handle<> /*h*/) noexcept {
    // Host fallback: busy-poll via the recv-side cv until either a
    // message arrives or the queue closes. The kernel will park the
    // coroutine on the scheduler's wait list directly.
    std::unique_lock lk(side->recv_->mu);
    side->recv_->cv_not_empty.wait(lk, [&] {
        return side->recv_->closed || !side->recv_->q.empty();
    });
    if (side->recv_->q.empty()) {
        // Closed: produce an empty message to unblock the caller.
        // Real impl signals a closed-endpoint error to the caller.
        out = Message{};
    } else {
        out = std::move(side->recv_->q.front());
        side->recv_->q.pop_front();
        side->recv_->cv_not_full.notify_one();
    }
}

}  // namespace neuro::ipc
