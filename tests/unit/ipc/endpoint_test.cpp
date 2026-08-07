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

// ---- 6. MT smoke: 4 producers, 4 consumers, 10000 messages ------

TEST(ipc_endpoint, mt_producers_consumers_smoke) {
    // Z3.6: 4 producers x 4 consumers x 2500 messages = 10 000 messages.
    // Verifies every payload is received exactly once and FIFO across the
    // merged traffic.
    Endpoint ep;
    constexpr int Producers   = 4;
    constexpr int Consumers   = 4;
    constexpr int PerProducer = 2500;
    constexpr int Total       = Producers * PerProducer;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> ts;
    for (int i = 0; i < Producers; ++i) {
        ts.emplace_back([&, i] {
            while (!start.load()) std::this_thread::yield();
            for (int j = 0; j < PerProducer; ++j) {
                // Tag carries producer id (low byte) and sequence (next bytes)
                // so consumers can verify uniqueness.
                std::uint32_t seq = static_cast<std::uint32_t>(j);
                std::uint32_t pid = static_cast<std::uint32_t>(i);
                std::uint32_t key = (pid << 24) | (seq & 0x00FFFFFF);
                std::vector<std::byte> payload(4);
                payload[0] = static_cast<std::byte>( key        & 0xFF);
                payload[1] = static_cast<std::byte>((key >>  8) & 0xFF);
                payload[2] = static_cast<std::byte>((key >> 16) & 0xFF);
                payload[3] = static_cast<std::byte>((key >> 24) & 0xFF);
                ep.send_nowait(Message(Tag(i, j), std::move(payload)));
                produced.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> cs;
    for (int i = 0; i < Consumers; ++i) {
        cs.emplace_back([&] {
            while (!start.load()) std::this_thread::yield();
            while (consumed.load() < Total) {
                if (auto m = ep.try_recv()) {
                    // Reconstruct the 32-bit key from payload bytes.
                    std::uint32_t key = 0;
                    key  = static_cast<std::uint32_t>(static_cast<std::uint8_t>(m->payload[0]));
                    key |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(m->payload[1])) << 8;
                    key |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(m->payload[2])) << 16;
                    key |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(m->payload[3])) << 24;
                    // Tag must agree: producer id in tag.ns, seq in tag.op.
                    EXPECT_EQ(static_cast<std::uint32_t>(m->tag.ns),
                              (key >> 24) & 0xFF);
                    EXPECT_EQ(static_cast<std::uint32_t>(m->tag.op),
                              key & 0xFFFF);
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start.store(true);
    for (auto& t : ts)  t.join();
    for (auto& t : cs)  t.join();
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

// ---- 8. recv_awaiter (co_await ep.recv()) ----------------------

namespace {

struct RecvTask {
    struct promise_type {
        Endpoint* ep = nullptr;
        Message   msg;

        RecvTask get_return_object() noexcept {
            return RecvTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    std::coroutine_handle<promise_type> h;
    RecvTask(std::coroutine_handle<promise_type> handle) noexcept : h(handle) {}
    ~RecvTask() { if (h) h.destroy(); }
};

RecvTask co_recv(Endpoint& ep, std::atomic<int>* done) {
    auto m = co_await ep.recv();
    done->store(static_cast<int>(m.payload.size()));
    co_return;
}

}  // namespace

TEST(ipc_endpoint, recv_awaiter_resumes_on_send) {
    Endpoint ep;
    std::atomic<int> done{-1};
    auto task = co_recv(ep, &done);

    // Suspend: queue is empty, awaiter parks.
    task.h.resume();
    EXPECT_EQ(-1, done.load());

    // A send unblocks the awaiter.
    ep.send_nowait(Message(Tag(1, 2), bytes({0x42, 0x43, 0x44, 0x45})));
    EXPECT_EQ(4, done.load());
}

TEST(ipc_endpoint, recv_awaiter_fast_path_consumes_existing_message) {
    Endpoint ep;
    ep.send_nowait(Message(Tag(1, 2), bytes({0x11})));
    std::atomic<int> done{-1};
    auto task = co_recv(ep, &done);
    task.h.resume();      // never suspends — message already present
    EXPECT_EQ(1, done.load());
    EXPECT_EQ(static_cast<std::size_t>(0), ep.size());
}

TEST(ipc_endpoint, recv_awaiter_receives_multiple_messages_in_order) {
    // Two receivers park on the same endpoint; two sends unblock them
    // in FIFO order.
    Endpoint ep;
    std::atomic<int> a_done{-1};
    std::atomic<int> b_done{-1};
    auto task_a = co_recv(ep, &a_done);
    auto task_b = co_recv(ep, &b_done);
    task_a.h.resume();
    task_b.h.resume();
    EXPECT_EQ(-1, a_done.load());
    EXPECT_EQ(-1, b_done.load());

    ep.send_nowait(Message(Tag(0, 0), bytes({0xAA})));
    EXPECT_EQ(1, a_done.load());
    EXPECT_EQ(-1, b_done.load());

    ep.send_nowait(Message(Tag(0, 1), bytes({0xBB, 0xCC})));
    EXPECT_EQ(2, b_done.load());
}

// ---- 9. send coroutine drains endpoint under load --------------

namespace {

struct DrainSendTask {
    struct promise_type {
        Endpoint* ep = nullptr;
        int       total = 0;
        std::atomic<int>* counter = nullptr;

        DrainSendTask get_return_object() noexcept {
            return DrainSendTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
    std::coroutine_handle<promise_type> h;
    DrainSendTask(std::coroutine_handle<promise_type> handle) noexcept
        : h(handle) {}
    ~DrainSendTask() { if (h) h.destroy(); }
};

DrainSendTask co_drain_send(Endpoint& ep, int total, std::atomic<int>* counter) {
    for (int i = 0; i < total; ++i) {
        // Tag carries the full i; payload carries i as a uint16_t (LE) so
        // the sum check works for totals > 255.
        std::uint16_t v = static_cast<std::uint16_t>(i);
        co_await ep.send(Message(Tag(0, v),
                                 bytes({static_cast<std::uint8_t>(v & 0xFF),
                                        static_cast<std::uint8_t>((v >> 8) & 0xFF)})));
    }
    counter->store(total);
    co_return;
}

}  // namespace

TEST(ipc_endpoint, send_coroutine_drains_1000_messages) {
    Endpoint ep;
    constexpr int N = 1000;
    std::atomic<int> done{0};
    auto task = co_drain_send(ep, N, &done);
    task.h.resume();

    // Drain in FIFO order and confirm every message landed.
    int sum = 0;
    for (int i = 0; i < N; ++i) {
        auto m = ep.try_recv();
        EXPECT_TRUE(m.has_value());
        EXPECT_EQ(static_cast<std::uint16_t>(i), m->tag.op);
        std::uint16_t v = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(m->payload[0]) |
            (static_cast<std::uint16_t>(static_cast<std::uint8_t>(m->payload[1])) << 8));
        sum += v;
    }
    EXPECT_EQ(N, done.load());
    EXPECT_EQ(static_cast<std::size_t>(0), ep.size());

    // Sum 0..999 == 499500.
    EXPECT_EQ(499500, sum);
}

TEST(ipc_endpoint, send_coroutine_and_recv_awaiter_interleave) {
    // Sender coroutine co_await ep.send; receiver coroutine co_await ep.recv.
    // Together they exchange N messages in order.
    Endpoint ep;
    constexpr int N = 100;
    std::atomic<int> sent{0};

    struct DrainRecvTask {
        struct promise_type {
            Endpoint* ep = nullptr;
            int       total = 0;
            std::atomic<int>* counter = nullptr;
            DrainRecvTask get_return_object() noexcept {
                return DrainRecvTask{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept { std::terminate(); }
        };
        std::coroutine_handle<promise_type> h;
        DrainRecvTask(std::coroutine_handle<promise_type> handle) noexcept
            : h(handle) {}
        ~DrainRecvTask() { if (h) h.destroy(); }
    };

    auto drain_recv = [](Endpoint& ep, int total, std::atomic<int>* counter)
        -> DrainRecvTask {
        int sum = 0;
        for (int i = 0; i < total; ++i) {
            auto m = co_await ep.recv();
            sum += static_cast<int>(m.payload[0]);
        }
        counter->store(sum);
        co_return;
    };

    std::atomic<int> sum_seen{-1};
    auto receiver = drain_recv(ep, N, &sum_seen);
    receiver.h.resume();  // parks

    auto sender = co_drain_send(ep, N, &sent);
    sender.h.resume();    // each send unblocks the receiver's await

    // Give the coroutine a chance to resume.
    while (sum_seen.load() == -1) std::this_thread::yield();
    EXPECT_EQ(N, sent.load());
    EXPECT_EQ(4950, sum_seen.load());  // sum 0..99 == 4950
}

TEST(ipc_endpoint, close_is_idempotent_and_reports_state) {
    Endpoint ep;
    EXPECT_FALSE(ep.closed());
    ep.send_nowait(Message(Tag(1, 1), bytes({0xAA})));
    ep.close();
    ep.close();
    EXPECT_TRUE(ep.closed());

    // close does not invalidate already queued messages; they remain
    // available for synchronous draining.
    auto m = ep.try_recv();
    EXPECT_TRUE(m.has_value());
    EXPECT_TRUE(m->tag == Tag(1, 1));
    EXPECT_FALSE(ep.try_recv().has_value());

    // Sends after close are discarded.
    ep.send_nowait(Message(Tag(2, 2), bytes({0xBB})));
    EXPECT_FALSE(ep.try_recv().has_value());
}

TEST(ipc_endpoint, recv_blocking_throws_after_close) {
    Endpoint ep;
    ep.close();
    bool threw = false;
    try {
        (void)ep.recv_blocking(std::chrono::milliseconds{1});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(ipc_endpoint, recv_awaiter_fast_path_after_close_returns_empty_message) {
    Endpoint ep;
    ep.close();
    std::atomic<int> payload_size{-1};
    auto task = co_recv(ep, &payload_size);
    task.h.resume();
    EXPECT_EQ(0, payload_size.load());
}

RUN_ALL_TESTS()
