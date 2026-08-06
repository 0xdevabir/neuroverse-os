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
// So:
//   pair.a().send_nowait(m)  →  pair.b().try_recv() sees m.
//   pair.b().send_nowait(m)  →  pair.a().try_recv() sees m.

#pragma once

#include <chrono>
#include <coroutine>
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
    // The transport queue sits in a shared_ptr so each Side can hold
    // an alias to it independently.
    struct Queue {
        std::mutex          mu;
        std::deque<Message> q;
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

            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>) const noexcept {
                side->send_nowait(std::move(msg));
            }
            void await_resume() const noexcept {
                side->send_nowait(std::move(msg));
            }
        };
        [[nodiscard]] send_awaiter send(Message m) {
            return send_awaiter{this, std::move(m)};
        }

        // ---- Synchronous send / recv --------------------------------
        void send_nowait(Message m) {
            std::lock_guard lk(send_->mu);
            send_->q.push_back(std::move(m));
        }

        [[nodiscard]] std::optional<Message> try_recv() {
            std::lock_guard lk(recv_->mu);
            if (recv_->q.empty()) return std::nullopt;
            Message m = std::move(recv_->q.front());
            recv_->q.pop_front();
            return m;
        }

        Message recv_blocking(std::chrono::milliseconds poll_interval =
                                  std::chrono::milliseconds{1}) {
            for (;;) {
                if (auto m = try_recv()) return std::move(*m);
                std::this_thread::sleep_for(poll_interval);
            }
        }

        // Number of messages waiting for *this* side to read.
        [[nodiscard]] std::size_t recv_queue_size() const {
            std::lock_guard lk(recv_->mu);
            return recv_->q.size();
        }

    private:
        std::shared_ptr<Queue> send_;
        std::shared_ptr<Queue> recv_;
    };

    EndpointPair()
        : fwd_(std::make_shared<Queue>()),
          back_(std::make_shared<Queue>()) {}

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

private:
    std::shared_ptr<Queue> fwd_;
    std::shared_ptr<Queue> back_;
};

}  // namespace neuro::ipc
