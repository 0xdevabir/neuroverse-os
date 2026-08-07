// tests/unit/ipc/endpoint_test.cpp
//
// Direct unit tests for neuro::ipc::Endpoint — the typed async
// message queue with co_await-aware send_awaiter.
//
// The integration tests in tests/integration/ipc_*.cpp exercise the
// Endpoint indirectly via EndpointPair; here we isolate the contract:
//   - default-constructed endpoint has size 0
//   - send_nowait / try_recv round-trip
//   - try_recv on empty returns nullopt
//   - FIFO ordering under load
//   - size() reflects queue depth
//   - cap-carrying messages round-trip
//   - send_nowait is thread-safe (MT smoke)

#include "neuro/ipc/endpoint.hpp"
#include "neuro/ipc/message.hpp"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::ipc::CapRef;
using neuro::ipc::Endpoint;
using neuro::ipc::Message;
using neuro::ipc::Tag;

namespace {

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> in) {
    std::vector<std::byte> out;
    for (auto b : in) out.push_back(static_cast<std::byte>(b));
    return out;
}

}  // namespace

// ---- 1. default state -------------------------------------------

TEST(ipc_endpoint, default_size_zero) {
    Endpoint ep;
    EXPECT_EQ(static_cast<std::size_t>(0), ep.size());
    EXPECT_FALSE(ep.try_recv().has_value());
}

TEST(ipc_endpoint, default_ctor_non_copyable_non_movable) {
    static_assert(!std::is_copy_constructible_v<Endpoint>);
    static_assert(!std::is_copy_assignable_v<Endpoint>);
    static_assert(!std::is_move_constructible_v<Endpoint>);
    static_assert(!std::is_move_assignable_v<Endpoint>);
}

// ---- 2. send_nowait + try_recv round trip -----------------------

TEST(ipc_endpoint, send_nowait_then_try_recv) {
    Endpoint ep;
    Message m(Tag(1, 2), bytes({0xAA, 0xBB}));
    ep.send_nowait(std::move(m));
    EXPECT_EQ(static_cast<std::size_t>(1), ep.size());

    auto got = ep.try_recv();
    EXPECT_TRUE(got.has_value());
    EXPECT_TRUE(got->tag == Tag(1, 2));
    EXPECT_EQ(static_cast<std::size_t>(2), got->payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xAA),
              static_cast<std::uint8_t>(got->payload[0]));
    EXPECT_EQ(static_cast<std::size_t>(0), ep.size());
}

TEST(ipc_endpoint, try_recv_on_empty_returns_nullopt) {
    Endpoint ep;
    EXPECT_FALSE(ep.try_recv().has_value());
    EXPECT_FALSE(ep.try_recv().has_value());
}

// ---- 3. FIFO ordering under load ---------------------------------

TEST(ipc_endpoint, fifo_ordering_preserved) {
    Endpoint ep;
    constexpr int N = 256;
    for (int i = 0; i < N; ++i) {
        ep.send_nowait(Message(Tag(0, static_cast<std::uint16_t>(i)),
                               bytes({static_cast<std::uint8_t>(i)})));
    }
    EXPECT_EQ(static_cast<std::size_t>(N), ep.size());

    for (int i = 0; i < N; ++i) {
        auto m = ep.try_recv();
        EXPECT_TRUE(m.has_value());
        EXPECT_EQ(static_cast<std::uint16_t>(i), m->tag.op);
        EXPECT_EQ(static_cast<std::uint8_t>(i),
                  static_cast<std::uint8_t>(m->payload[0]));
    }
}

// ---- 4. cap-carrying messages ------------------------------------

TEST(ipc_endpoint, cap_carrying_message_round_trip) {
    Endpoint ep;
    Message m(Tag(3, 4), CapRef{0xDEADBEEFCAFEBABEULL});
    ep.send_nowait(std::move(m));

    auto got = ep.try_recv();
    EXPECT_TRUE(got.has_value());
    EXPECT_TRUE(got->carries_cap());
    EXPECT_TRUE(got->cap.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0xDEADBEEFCAFEBABEULL),
              got->cap->handle);
}

// ---- 5. recv_blocking unblocks on message ------------------------

TEST(ipc_endpoint, recv_blocking_unblocks_on_send) {
    Endpoint ep;
    std::atomic<bool> received{false};

    std::thread receiver([&] {
        auto m = ep.recv_blocking(std::chrono::milliseconds{1});
        received.store(true);
        EXPECT_TRUE(m.tag == Tag(7, 8));
    });

    // Give the receiver a moment to start polling.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(received.load());

    ep.send_nowait(Message(Tag(7, 8)));

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds{2};
    while (!received.load() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(received.load());
    receiver.join();
}

// ---- 6. MT smoke: 4 producers, 4 consumers, 1000 messages ------

TEST(ipc_endpoint, mt_producers_consumers_smoke) {
    Endpoint ep;
    constexpr int Producers = 4;
    constexpr int Consumers = 4;
    constexpr int PerProducer = 250;
    constexpr int Total = Producers * PerProducer;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> ts;
    for (int i = 0; i < Producers; ++i) {
        ts.emplace_back([&, i] {
            while (!start.load()) std::this_thread::yield();
            for (int j = 0; j < PerProducer; ++j) {
                ep.send_nowait(Message(Tag(i, j), bytes({0xFF})));
                produced.fetch_add(1);
            }
        });
    }
    for (int i = 0; i < Consumers; ++i) {
        ts.emplace_back([&] {
            while (!start.load()) std::this_thread::yield();
            while (consumed.load() < Total) {
                if (auto m = ep.try_recv()) {
                    consumed.fetch_add(1);
                    (void)m;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true);
    for (auto& t : ts) t.join();
    EXPECT_EQ(Total,  produced.load());
    EXPECT_EQ(Total,  consumed.load());
    EXPECT_EQ(0u,     ep.size());
}

// ---- 7. send_awaiter (co_await ep.send(msg)) --------------------

#include <coroutine>

namespace {

struct SendTask {
    struct promise_type {
        Endpoint* ep = nullptr;
        Message   msg;

        SendTask get_return_object() noexcept {
            return SendTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    std::coroutine_handle<promise_type> h;
    SendTask(std::coroutine_handle<promise_type> handle) noexcept : h(handle) {}
    ~SendTask() { if (h) h.destroy(); }
};

SendTask co_send(Endpoint& ep, Message m) {
    co_await ep.send(std::move(m));
    co_return;
}

}  // namespace

TEST(ipc_endpoint, co_await_send_queues_message) {
    Endpoint ep;
    {
        SendTask t = co_send(ep, Message(Tag(9, 10), bytes({0x42})));
        // The coroutine body runs on first resume; we drive it manually.
        t.h.resume();
    }
    EXPECT_EQ(static_cast<std::size_t>(1), ep.size());
    auto m = ep.try_recv();
    EXPECT_TRUE(m.has_value());
    EXPECT_TRUE(m->tag == Tag(9, 10));
}

RUN_ALL_TESTS()
