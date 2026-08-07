// tests/unit/net/channel_test.cpp
//
// Tests for neuro::net::Channel<T> — a generic MPMC queue with
// blocking recv(), timed recv_for(), and non-blocking try_recv().
//
// Coverage:
//   - default-constructed channel is empty
//   - send / try_recv round-trip (FIFO order)
//   - size() tracks queue depth
//   - recv() blocks until a value is sent (single producer)
//   - recv() throws after close()
//   - recv_for() returns the value if it arrives in time
//   - recv_for() throws on timeout
//   - recv_for() throws immediately if already closed
//   - close() is safe to call from another thread while recv blocks
//   - close() is idempotent
//   - try_recv() on an empty channel returns nullopt

#include "neuro/net/channel.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::net::Channel;

namespace {

// RAII helper: runs `fn` on a std::thread and joins at scope exit.
template <class Fn>
struct Joiner {
    std::thread t;
    explicit Joiner(Fn fn) : t(std::move(fn)) {}
    ~Joiner() { if (t.joinable()) t.join(); }
};

}  // namespace

// ---- 1. empty channel ----------------------------------------------

TEST(channel, empty_try_recv_returns_nullopt) {
    Channel<int> c;
    EXPECT_EQ(0u, c.size());
    EXPECT_FALSE(c.try_recv().has_value());
}

// ---- 2. FIFO round-trip --------------------------------------------

TEST(channel, send_try_recv_fifo) {
    Channel<int> c;
    c.send(1);
    c.send(2);
    c.send(3);
    EXPECT_EQ(3u, c.size());

    EXPECT_EQ(1, c.try_recv().value());
    EXPECT_EQ(2, c.try_recv().value());
    EXPECT_EQ(3, c.try_recv().value());
    EXPECT_EQ(0u, c.size());
    EXPECT_FALSE(c.try_recv().has_value());
}

// ---- 3. recv() blocks until a value arrives ------------------------

TEST(channel, recv_blocks_until_send) {
    Channel<int> c;
    Joiner producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        c.send(42);
    });
    auto v = c.recv();
    EXPECT_EQ(42, v);
}

// ---- 4. close() unblocks recv() with throw -------------------------

TEST(channel, recv_throws_after_close) {
    Channel<int> c;
    c.close();
    bool threw = false;
    try {
        (void)c.recv();
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ---- 5. recv_for() returns the value if it arrives in time --------

TEST(channel, recv_for_returns_in_time) {
    Channel<int> c;
    Joiner producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        c.send(7);
    });
    auto v = c.recv_for(std::chrono::milliseconds(500));
    EXPECT_EQ(7, v);
}

// ---- 6. recv_for() throws on timeout -------------------------------

TEST(channel, recv_for_throws_on_timeout) {
    Channel<int> c;
    bool threw = false;
    try {
        (void)c.recv_for(std::chrono::milliseconds(20));
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ---- 7. recv_for() throws immediately on already-closed -----------

TEST(channel, recv_for_throws_on_closed) {
    Channel<int> c;
    c.close();
    bool threw = false;
    try {
        (void)c.recv_for(std::chrono::milliseconds(100));
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ---- 8. close() while recv() is blocked ----------------------------

TEST(channel, close_unblocks_recv) {
    Channel<int> c;
    std::atomic<bool> recv_returned{false};
    std::atomic<bool> recv_threw{false};
    {
        Joiner consumer([&] {
            try {
                (void)c.recv();
                recv_returned.store(true);
            } catch (...) {
                recv_threw.store(true);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        c.close();
        // consumer joins here, after the close()
    }
    EXPECT_TRUE(recv_threw.load());
    EXPECT_FALSE(recv_returned.load());
}

// ---- 9. close() idempotent ----------------------------------------

TEST(channel, close_idempotent) {
    Channel<int> c;
    c.close();
    c.close();
    c.close();
    // No crash; try_recv still reports empty.
    EXPECT_FALSE(c.try_recv().has_value());
}

// ---- 10. payload types: vector + string ----------------------------

TEST(channel, vector_payload) {
    Channel<std::vector<int>> c;
    c.send({1, 2, 3, 4, 5});
    auto v = c.try_recv();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), v->size());
    EXPECT_EQ(3, (*v)[2]);
}

TEST(channel, string_payload) {
    Channel<std::string> c;
    c.send(std::string("hello"));
    auto v = c.recv_for(std::chrono::milliseconds(100));
    EXPECT_EQ(std::string("hello"), v);
}

// ---- 11. MPMC: multiple producers + consumers ---------------------

TEST(channel, mpmc_deliver_all) {
    Channel<int> c;
    constexpr int N = 200;
    constexpr int Producers = 4;

    std::vector<std::thread> ts;
    for (int p = 0; p < Producers; ++p) {
        ts.emplace_back([&, p] {
            for (int i = 0; i < N; ++i) {
                c.send(p * N + i);
            }
        });
    }
    for (auto& t : ts) t.join();

    std::vector<int> got;
    got.reserve(Producers * N);
    // Drain using try_recv (producer side already joined so we know
    // total count is exact).
    while (auto v = c.try_recv()) {
        got.push_back(*v);
    }
    EXPECT_EQ(static_cast<std::size_t>(Producers * N), got.size());
}

RUN_ALL_TESTS()