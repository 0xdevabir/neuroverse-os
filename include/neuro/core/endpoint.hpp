// neuro/core/endpoint.hpp
//
// Endpoint: typed IPC channel backed by a mutex-guarded message queue.
// Mirrors README §9.3. Full async/coroutine rewrite arrives in Phase F.

#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace neuro::core {

struct Message {
    std::uint64_t            tag;
    std::vector<std::byte>   payload;
};

class Endpoint {
public:
    Endpoint() = default;
    Endpoint(const Endpoint&)            = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint(Endpoint&&)                 = delete;
    Endpoint& operator=(Endpoint&&)      = delete;

    void send(Message m) {
        std::lock_guard lk(mu_);
        queue_.push_back(std::move(m));
    }

    [[nodiscard]] std::optional<Message> try_recv() {
        std::lock_guard lk(mu_);
        if (queue_.empty()) return std::nullopt;
        Message m = std::move(queue_.front());
        queue_.pop_front();
        return m;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mu_);
        return queue_.size();
    }

private:
    mutable std::mutex   mu_;
    std::deque<Message>  queue_;
};

}  // namespace neuro::core