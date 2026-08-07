// neuro/sched/deadline.hpp
//
// DeadlineQueue: a min-heap of (deadline, sequence) entries that the
// caller pops in deadline order. A pop on an entry whose deadline is
// in the future either returns nullopt (try_pop) or returns it
// immediately (pop) — the queue is not a timer, just an ordered view.
//
// Phase H primitive. Used by NeuroLearn's optimizer to schedule
// re-evaluations at monotonic time points, and by timed-awaiter
// coroutines in the IPC stack to gate resumption on a deadline.
//
// Thread-safety: not thread-safe by default. Wrap with a mutex if a
// single producer / consumer is not enough.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace neuro::sched {

template <class T>
class DeadlineQueue {
public:
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;

    struct Entry {
        time_point deadline;
        std::uint64_t seq;     // tie-breaker: earlier pushes first
        T           value;

        // Returns true when THIS entry has HIGHER priority than
        // `other` — i.e. THIS entry should be popped first.
        [[nodiscard]] bool higher_priority_than(const Entry& other) const noexcept {
            if (deadline != other.deadline) return deadline < other.deadline;
            return seq < other.seq;
        }
    };

    void push(time_point deadline, T value) {
        entries_.push_back(Entry{deadline, next_seq_++,
                                  std::move(value)});
        sift_up(entries_.size() - 1);
    }

    // Pop the earliest-deadline entry regardless of whether its
    // deadline has elapsed. Useful for draining.
    [[nodiscard]] std::optional<T> pop() {
        if (entries_.empty()) return std::nullopt;
        Entry top = std::move(entries_.front());
        if (entries_.size() > 1) {
            entries_.front() = std::move(entries_.back());
        }
        entries_.pop_back();
        if (!entries_.empty()) sift_down(0);
        return std::move(top.value);
    }

    // Pop only if the deadline has elapsed. Returns nullopt if the
    // next entry is in the future or the queue is empty.
    [[nodiscard]] std::optional<T> try_pop() {
        if (entries_.empty()) return std::nullopt;
        if (entries_.front().deadline > clock::now()) return std::nullopt;
        return pop();
    }

    // Look at the next deadline without removing it.
    [[nodiscard]] std::optional<time_point> next_deadline() const noexcept {
        if (entries_.empty()) return std::nullopt;
        return entries_.front().deadline;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return entries_.empty(); }

    // Number of entries whose deadline has elapsed. (The queue is a
    // heap, not sorted; we scan in heap order and stop at the first
    // not-yet-elapsed entry that we encounter — only works if all
    // ready entries happen to come first in heap order, which is
    // NOT guaranteed. Use this as a rough lower bound only.)
    [[nodiscard]] std::size_t ready_count() const noexcept {
        const auto now = clock::now();
        std::size_t n = 0;
        // Heuristic: scan all entries; this is O(n) but the queue is
        // typically small.
        for (const auto& e : entries_) {
            if (e.deadline <= now) ++n;
        }
        return n;
    }

    void clear() noexcept { entries_.clear(); }

private:
    void sift_up(std::size_t i) {
        while (i > 0) {
            const std::size_t parent = (i - 1) / 2;
            if (entries_[i].higher_priority_than(entries_[parent])) {
                std::swap(entries_[i], entries_[parent]);
                i = parent;
            } else break;
        }
    }

    void sift_down(std::size_t i) {
        const std::size_t n = entries_.size();
        for (;;) {
            std::size_t child = 2 * i + 1;
            if (child >= n) return;
            if (child + 1 < n &&
                entries_[child + 1].higher_priority_than(entries_[child])) {
                ++child;
            }
            if (entries_[child].higher_priority_than(entries_[i])) {
                std::swap(entries_[i], entries_[child]);
                i = child;
            } else return;
        }
    }

    std::vector<Entry> entries_;
    std::uint64_t      next_seq_ = 0;
};

}  // namespace neuro::sched
