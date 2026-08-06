// neuro/net/channel.hpp
//
// Generic MPMC queue with blocking and timed receives.
// Mirrors README §9.5. Used both by net stacks and as a synchronisation
// primitive across subsystems.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace neuro::net {

template <class T>
class Channel {
public:
    Channel() = default;
    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    void send(T value) {
        std::lock_guard lk(mu_);
        queue_.push(std::move(value));
        cv_.notify_one();
    }

    [[nodiscard]] std::optional<T> try_recv() {
        std::lock_guard lk(mu_);
        if (queue_.empty()) return std::nullopt;
        T v = std::move(queue_.front());
        queue_.pop();
        return v;
    }

    [[nodiscard]] T recv() {
        std::unique_lock lk(mu_);
        cv_.wait(lk, [&] { return !queue_.empty() || closed_; });
        if (queue_.empty()) {
            throw std::runtime_error("recv: channel closed");
        }
        T v = std::move(queue_.front());
        queue_.pop();
        return v;
    }

    [[nodiscard]] T recv_for(std::chrono::milliseconds d) {
        std::unique_lock lk(mu_);
        if (!cv_.wait_for(lk, d, [&] { return !queue_.empty() || closed_; })) {
            throw std::runtime_error("recv_for: timeout");
        }
        if (queue_.empty()) {
            throw std::runtime_error("recv_for: channel closed");
        }
        T v = std::move(queue_.front());
        queue_.pop();
        return v;
    }

    void close() {
        std::lock_guard lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mu_);
        return queue_.size();
    }

private:
    mutable std::mutex                mu_;
    std::condition_variable           cv_;
    std::queue<T>                     queue_;
    std::atomic<bool>                 closed_{false};
};

}  // namespace neuro::net