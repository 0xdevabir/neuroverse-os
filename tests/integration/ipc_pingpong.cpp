// tests/integration/ipc_pingpong.cpp
//
// Ping-pong across an EndpointPair.
//
// Verifies:
//   1. Ordering: N pings sent from A -> B are received in the same
//      order they were sent.
//   2. Bidirectionality: replies sent from B -> A arrive correctly.
//   3. Cap pass-through: a cap reference in the payload round-trips
//      with the original handle intact.
//   4. async send (co_await): a coroutine sender delivers every
//      message before the receiver thread returns.

#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/ipc/endpoint_pair.hpp"
#include "neuro/ipc/message.hpp"

using neuro::ipc::CapRef;
using neuro::ipc::EndpointPair;
using neuro::ipc::Message;
using neuro::ipc::Tag;

namespace {

constexpr Tag PING{0x0001, 0x0001};
constexpr Tag PONG{0x0001, 0x0002};
constexpr Tag CAP_PING{0x0001, 0x0010};

// ---- coroutine sender ----------------------------------------------------

struct SenderTask {
    struct promise_type {
        SenderTask get_return_object() {
            return SenderTask{handle_type::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never  final_suspend()   noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type h = nullptr;
};

SenderTask ping_n(EndpointPair::Side side, int n) {
    for (int i = 0; i < n; ++i) {
        std::vector<std::byte> bytes(2);
        bytes[0] = std::byte{0xFF};
        bytes[1] = std::byte{static_cast<unsigned char>(i & 0xFF)};
        co_await side.send(Message::bytes(PING, std::move(bytes)));
    }
}

SenderTask reply_n(EndpointPair::Side side, int n) {
    for (int i = 0; i < n; ++i) {
        std::vector<std::byte> bytes(2);
        bytes[0] = std::byte{0xEE};
        bytes[1] = std::byte{static_cast<unsigned char>(i & 0xFF)};
        co_await side.send(Message::bytes(PONG, std::move(bytes)));
    }
}

}  // namespace

TEST(pingpong, ordering_preserved_one_way) {
    EndpointPair pair;

    constexpr int kN = 32;
    std::thread sender([&] {
        auto t = ping_n(pair.a(), kN);
        (void)t.h;
    });

    auto recv = pair.b();
    for (int i = 0; i < kN; ++i) {
        Message m = recv.recv_blocking();
        EXPECT_EQ(m.tag, PING);
        EXPECT_EQ(m.size(), 2u);
        EXPECT_EQ(static_cast<unsigned>(m.payload[0]), 0xFFu);
        EXPECT_EQ(static_cast<unsigned>(m.payload[1]),
                  static_cast<unsigned>(i & 0xFF));
    }

    sender.join();
    EXPECT_EQ(pair.a_unread(), 0u);
    EXPECT_EQ(pair.b_unread(), 0u);
}

TEST(pingpong, bidirectional_round_trip) {
    EndpointPair pair;

    constexpr int kN = 16;
    std::thread a_thread([&] {
        auto t = ping_n(pair.a(), kN);
        (void)t.h;
    });
    std::thread b_thread([&] {
        auto recv = pair.b();
        // First receive kN pings, then send kN pongs.
        for (int i = 0; i < kN; ++i) {
            Message m = recv.recv_blocking();
            EXPECT_EQ(m.tag, PING);
        }
        auto t = reply_n(pair.b(), kN);
        (void)t.h;
    });

    auto recv = pair.a();
    for (int i = 0; i < kN; ++i) {
        Message m = recv.recv_blocking();
        EXPECT_EQ(m.tag, PONG);
        EXPECT_EQ(static_cast<unsigned>(m.payload[0]), 0xEEu);
    }

    a_thread.join();
    b_thread.join();
}

TEST(pingpong, capability_reference_passes_through) {
    EndpointPair pair;
    constexpr std::uint64_t kHandle = 0xDEADBEEFCAFEBABEull;

    std::thread sender([&] {
        pair.a().send_nowait(Message::with_cap(CAP_PING, CapRef{kHandle}));
    });

    Message m = pair.b().recv_blocking();
    EXPECT_EQ(m.tag, CAP_PING);
    EXPECT_TRUE(m.carries_cap());
    EXPECT_TRUE(m.cap.has_value());
    EXPECT_EQ(m.cap->handle, kHandle);

    sender.join();
}

TEST(pingpong, async_send_coroutine_drains_completely) {
    // Use the async send (co_await) inside a coroutine and ensure
    // every message lands in the receiver's queue.
    EndpointPair pair;

    constexpr int kN = 64;
    SenderTask t = ping_n(pair.a(), kN);
    (void)t.h;

    int received = 0;
    std::thread receiver([&] {
        auto recv = pair.b();
        for (int i = 0; i < kN; ++i) {
            Message m = recv.recv_blocking();
            EXPECT_EQ(m.tag, PING);
            ++received;
        }
    });
    receiver.join();

    EXPECT_EQ(received, kN);
    EXPECT_EQ(pair.a_unread(), 0u);
    EXPECT_EQ(pair.b_unread(), 0u);
}

RUN_ALL_TESTS()
