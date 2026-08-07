// tests/unit/ipc/endpoint_pair_test.cpp
//
// Tests for neuro::ipc::EndpointPair — a pair of typed endpoints
// pre-wired for bidirectional IPC, with bounded capacity +
// backpressure + close.
//
// Coverage:
//   - default + explicit capacity
//   - a() sends to b() and vice-versa
//   - per-side send / recv queue sizes
//   - send_nowait returns false when full (backpressure)
//   - try_send_for succeeds when there's space, false on timeout
//   - try_recv_for blocks until a message arrives
//   - recv_blocking returns next message
//   - close(): send / recv return false / nullopt
//   - a_send_full / b_send_full capacity predicates
//   - MPSC stress: 4 producers to b(), 1 receiver from a()

#include "neuro/ipc/endpoint_pair.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::ipc::EndpointPair;
using neuro::ipc::Message;
using neuro::ipc::Tag;

namespace {

Message make_msg(std::uint16_t ns, std::uint16_t op,
                 std::uint64_t payload = 0) {
    Message m{Tag{ns, op}};
    for (int i = 0; i < 8; ++i) {
        std::uint8_t b = static_cast<std::uint8_t>((payload >> (i * 8)) & 0xFF);
        m.payload.push_back(static_cast<std::byte>(b));
    }
    return m;
}

}  // namespace

// ---- 1. default + explicit capacity -------------------------------

TEST(endpoint_pair, default_capacity_is_64) {
    EndpointPair p;
    EXPECT_EQ(EndpointPair::kDefaultCapacity, p.a().send_capacity());
    EXPECT_EQ(EndpointPair::kDefaultCapacity, p.b().send_capacity());
}

TEST(endpoint_pair, send_queue_size_grows_under_load) {
    // Z3.5: send_queue_size reflects queued messages and drains to 0
    // as the receiver pulls them out.
    EndpointPair p(16);
    auto a = p.a();
    auto b = p.b();

    EXPECT_EQ(0u, a.send_queue_size());
    for (int i = 0; i < 5; ++i) a.send_nowait(make_msg(0, static_cast<std::uint16_t>(i)));
    EXPECT_EQ(5u, a.send_queue_size());

    // b() reads from the forward queue, draining the send side.
    for (int i = 0; i < 5; ++i) {
        auto m = b.try_recv();
        EXPECT_TRUE(m.has_value());
    }
    EXPECT_EQ(0u, a.send_queue_size());
    EXPECT_EQ(0u, p.b_unread());
}

TEST(endpoint_pair, try_send_for_unblocks_on_close) {
    // Z3.4: a parked try_send_for must return false promptly when
    // the queue is closed from another thread.
    EndpointPair p(1);
    auto a = p.a();
    a.send_nowait(make_msg(0, 0));   // fill capacity
    EXPECT_TRUE(p.a_send_full());

    std::atomic<bool> returned{false};
    std::atomic<bool> closed{false};

    std::thread sender([&] {
        // try_send_for should eventually return false (closed).
        bool ok = a.try_send_for(make_msg(0, 1),
                                 std::chrono::seconds{2});
        returned.store(true);
        EXPECT_FALSE(ok);
    });

    // Give sender time to park on cv_not_full.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(returned.load());

    // Manually close the forward queue by destroying a clone of the
    // EndpointPair. EndpointPair dtor calls close() on both queues,
    // which notifies the parked sender.
    {
        EndpointPair p2(std::move(p));  // take ownership, will close on dtor
        closed.store(true);
    }
    sender.join();
    EXPECT_TRUE(closed.load());
    EXPECT_TRUE(returned.load());
}

TEST(endpoint_pair, try_recv_for_unblocks_on_close) {
    // Z3.4 (recv side): a parked try_recv_for returns nullopt when
    // the queue is closed from another thread.
    EndpointPair p;
    auto b = p.b();
    std::atomic<bool> returned{false};

    std::thread receiver([&] {
        auto m = b.try_recv_for(std::chrono::seconds{2});
        returned.store(true);
        EXPECT_FALSE(m.has_value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(returned.load());

    {
        EndpointPair p2(std::move(p));
    }
    receiver.join();
    EXPECT_TRUE(returned.load());
}

TEST(endpoint_pair, explicit_capacity) {
    EndpointPair p(8);
    EXPECT_EQ(static_cast<std::size_t>(8), p.a().send_capacity());
    EXPECT_EQ(static_cast<std::size_t>(8), p.b().send_capacity());
}

// ---- 2. a() -> b() round-trip -------------------------------------

TEST(endpoint_pair, a_to_b_round_trip) {
    EndpointPair p;
    auto a = p.a();
    auto b = p.b();
    EXPECT_TRUE(a.send_nowait(make_msg(1, 1, 0xDEAD)));
    auto m = b.try_recv();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(1u, m->tag.ns);
    EXPECT_EQ(1u, m->tag.op);
    EXPECT_EQ(8u, m->payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), static_cast<std::uint8_t>(m->payload[0]));
}

// ---- 3. b() -> a() round-trip -------------------------------------

TEST(endpoint_pair, b_to_a_round_trip) {
    EndpointPair p;
    auto a = p.a();
    auto b = p.b();
    EXPECT_TRUE(b.send_nowait(make_msg(2, 3, 0xCAFEBABE)));
    auto m = a.try_recv();
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(2u, m->tag.ns);
    EXPECT_EQ(3u, m->tag.op);
}

// ---- 4. queue sizes -----------------------------------------------

TEST(endpoint_pair, queue_sizes_track_messages) {
    EndpointPair p(4);
    auto a = p.a();
    auto b = p.b();
    EXPECT_EQ(0u, p.a_unread());  // messages a hasn't read
    EXPECT_EQ(0u, p.b_unread());

    EXPECT_TRUE(a.send_nowait(make_msg(0, 0)));
    EXPECT_TRUE(a.send_nowait(make_msg(0, 1)));
    EXPECT_EQ(2u, p.b_unread());
    EXPECT_EQ(2u, b.recv_queue_size());
    EXPECT_EQ(0u, a.recv_queue_size());
}

// ---- 5. send_nowait backpressure ----------------------------------

TEST(endpoint_pair, send_nowait_backpressure_when_full) {
    EndpointPair p(2);
    auto a = p.a();
    EXPECT_TRUE(a.send_nowait(make_msg(0, 0)));
    EXPECT_TRUE(a.send_nowait(make_msg(0, 1)));
    EXPECT_FALSE(a.send_nowait(make_msg(0, 2)));  // full
    EXPECT_TRUE(p.a_send_full());
}

// ---- 6. try_recv_for waits for a message --------------------------

TEST(endpoint_pair, try_recv_for_returns_message) {
    EndpointPair p;
    auto a = p.a();
    auto b = p.b();

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        a.send_nowait(make_msg(7, 7));
    });

    auto m = b.try_recv_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(m.has_value());
    EXPECT_EQ(7u, m->tag.ns);
    producer.join();
}

TEST(endpoint_pair, try_recv_for_times_out) {
    EndpointPair p;
    auto b = p.b();
    auto m = b.try_recv_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(m.has_value());
}

// ---- 7. try_send_for waits for space ------------------------------

TEST(endpoint_pair, try_send_for_blocks_until_drained) {
    EndpointPair p(2);
    auto a = p.a();
    auto b = p.b();

    EXPECT_TRUE(a.send_nowait(make_msg(0, 0)));
    EXPECT_TRUE(a.send_nowait(make_msg(0, 1)));

    std::thread receiver([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (void)b.try_recv();
        (void)b.try_recv();
    });

    EXPECT_TRUE(a.try_send_for(make_msg(0, 2),
                               std::chrono::milliseconds(500)));
    receiver.join();
}

TEST(endpoint_pair, try_send_for_timeout_when_full) {
    EndpointPair p(1);
    auto a = p.a();
    EXPECT_TRUE(a.send_nowait(make_msg(0, 0)));
    EXPECT_FALSE(a.try_send_for(make_msg(0, 1),
                                std::chrono::milliseconds(30)));
}

// ---- 8. recv_blocking ---------------------------------------------

TEST(endpoint_pair, recv_blocking_returns_next_message) {
    EndpointPair p;
    auto a = p.a();
    auto b = p.b();
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        a.send_nowait(make_msg(9, 9, 0x42));
    });
    auto m = b.recv_blocking();
    EXPECT_EQ(9u, m.tag.ns);
    producer.join();
}

// ---- 9. a() and b() are independent handles -----------------------

TEST(endpoint_pair, a_and_b_share_pair_but_are_distinct) {
    EndpointPair p;
    auto a1 = p.a();
    auto a2 = p.a();
    auto b = p.b();

    a1.send_nowait(make_msg(0, 0));
    a2.send_nowait(make_msg(0, 1));

    auto m1 = b.try_recv();
    auto m2 = b.try_recv();
    EXPECT_TRUE(m1.has_value());
    EXPECT_TRUE(m2.has_value());
    EXPECT_EQ(0u, m1->tag.op);
    EXPECT_EQ(1u, m2->tag.op);
}

// ---- 10. b_send_full / a_send_full are capacity-aware -----------

TEST(endpoint_pair, send_full_predicates) {
    EndpointPair p(3);
    auto a = p.a();
    auto b = p.b();
    EXPECT_FALSE(p.a_send_full());
    EXPECT_FALSE(p.b_send_full());

    a.send_nowait(make_msg(0, 0));
    a.send_nowait(make_msg(0, 1));
    EXPECT_FALSE(p.a_send_full());
    a.send_nowait(make_msg(0, 2));
    EXPECT_TRUE(p.a_send_full());

    b.send_nowait(make_msg(0, 0));
    b.send_nowait(make_msg(0, 1));
    EXPECT_FALSE(p.b_send_full());
}

// ---- 11. MPSC stress: 4 senders -> 1 receiver --------------------

TEST(endpoint_pair, mpsc_stress) {
    EndpointPair p(1024);
    auto a = p.a();
    auto b = p.b();

    constexpr int Producers = 4;
    constexpr int PerProducer = 50;
    constexpr int Total = Producers * PerProducer;

    std::vector<std::thread> ts;
    for (int i = 0; i < Producers; ++i) {
        ts.emplace_back([&, i] {
            for (int j = 0; j < PerProducer; ++j) {
                while (!a.try_send_for(make_msg(0, static_cast<std::uint16_t>(i)),
                                       std::chrono::seconds(2))) {}
            }
        });
    }
    for (auto& t : ts) t.join();

    int got = 0;
    while (got < Total) {
        auto m = b.try_recv_for(std::chrono::milliseconds(200));
        if (m) ++got;
        else   break;
    }
    EXPECT_EQ(Total, got);
}

RUN_ALL_TESTS()
