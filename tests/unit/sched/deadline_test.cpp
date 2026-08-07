// tests/unit/sched/deadline_test.cpp
//
// Unit tests for neuro::sched::DeadlineQueue — the min-heap deadline
// queue.

#include "neuro/sched/deadline.hpp"

#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::sched::DeadlineQueue;
using Clock      = std::chrono::steady_clock;
using time_point = Clock::time_point;

namespace {
time_point tp(std::chrono::milliseconds d) {
    return Clock::now() + d;
}
}  // namespace

// ---- 1. empty queue --------------------------------------------------

TEST(deadline, empty_queue_basics) {
    DeadlineQueue<int> q;
    EXPECT_EQ(0u, q.size());
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.pop().has_value());
    EXPECT_FALSE(q.try_pop().has_value());
    EXPECT_FALSE(q.next_deadline().has_value());
}

// ---- 2. push / pop preserves order ----------------------------------

TEST(deadline, push_pop_in_deadline_order) {
    DeadlineQueue<int> q;
    auto t0 = tp(std::chrono::milliseconds{10});
    q.push(t0, 7);

    auto t1 = tp(std::chrono::milliseconds{5});
    q.push(t1, 5);

    auto t2 = tp(std::chrono::milliseconds{20});
    q.push(t2, 20);

    auto a = q.pop();
    auto b = q.pop();
    auto c = q.pop();
    EXPECT_TRUE(a.has_value()); EXPECT_EQ(5,  *a);
    EXPECT_TRUE(b.has_value()); EXPECT_EQ(7,  *b);
    EXPECT_TRUE(c.has_value()); EXPECT_EQ(20, *c);
    EXPECT_TRUE(q.empty());
}

// ---- 3. equal-deadline FIFO by sequence number ----------------------

TEST(deadline, equal_deadline_fifo) {
    DeadlineQueue<int> q;
    auto t = Clock::now();
    q.push(t, 1);
    q.push(t, 2);
    q.push(t, 3);
    EXPECT_EQ(1, q.pop().value());
    EXPECT_EQ(2, q.pop().value());
    EXPECT_EQ(3, q.pop().value());
}

// ---- 4. try_pop respects the clock ---------------------------------

TEST(deadline, try_pop_returns_nullopt_for_future_deadline) {
    DeadlineQueue<int> q;
    q.push(tp(std::chrono::milliseconds{500}), 42);
    EXPECT_FALSE(q.try_pop().has_value());
    EXPECT_EQ(1u, q.size());
    auto v = q.pop();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(42, *v);
}

TEST(deadline, try_pop_succeeds_for_past_deadline) {
    DeadlineQueue<int> q;
    q.push(Clock::now() - std::chrono::milliseconds{10}, 7);
    EXPECT_TRUE(q.try_pop().has_value());
}

// ---- 5. ready_count ------------------------------------------------

TEST(deadline, ready_count_after_elapsed) {
    DeadlineQueue<int> q;
    q.push(tp(std::chrono::milliseconds{500}), 1);   // far future
    q.push(tp(std::chrono::milliseconds{50}),  2);   // still future after 10 ms
    q.push(tp(std::chrono::milliseconds{1}),   3);   // ready
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    // Only the 1 ms entry has elapsed.
    EXPECT_EQ(1u, q.ready_count());

    // After waiting past 50 ms, two entries are ready.
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    EXPECT_EQ(2u, q.ready_count());
}

// ---- 6. clear ------------------------------------------------------

TEST(deadline, clear_empties) {
    DeadlineQueue<int> q;
    for (int i = 0; i < 8; ++i) q.push(tp(std::chrono::milliseconds{i}), i);
    EXPECT_EQ(8u, q.size());
    q.clear();
    EXPECT_TRUE(q.empty());
}

// ---- 7. handles many items and pops all in order -------------------

TEST(deadline, many_items_pop_all_in_order) {
    DeadlineQueue<int> q;
    constexpr int N = 1000;
    // Insert in random order; pop should still be in deadline order.
    for (int i = 0; i < N; ++i) {
        q.push(tp(std::chrono::milliseconds{i}), i);
    }
    for (int i = 0; i < N; ++i) {
        auto v = q.pop();
        EXPECT_TRUE(v.has_value());
        EXPECT_EQ(i, *v);
    }
    EXPECT_TRUE(q.empty());
}

// ---- 8. next_deadline returns the earliest --------------------------

TEST(deadline, next_deadline_returns_earliest) {
    DeadlineQueue<int> q;
    auto t_early = tp(std::chrono::milliseconds{10});
    auto t_late  = tp(std::chrono::milliseconds{500});
    q.push(t_late,  1);
    q.push(t_early, 2);
    EXPECT_EQ(t_early, q.next_deadline().value());
    (void)q.pop();
    EXPECT_EQ(t_late, q.next_deadline().value());
}

RUN_ALL_TESTS()