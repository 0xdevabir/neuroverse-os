// tests/unit/core/endpoint_test.cpp
//
// Tests for neuro::core::Endpoint — the typed IPC channel from §9.3.
//
// Coverage:
//   - default-constructed endpoint is empty
//   - send / try_recv round-trip (FIFO order)
//   - size() tracks the queue depth
//   - try_recv on an empty queue returns nullopt
//   - non-copyable + non-movable (the endpoints are pinned by id)
//   - payload bytes survive a round-trip
//   - mixed-size messages dequeue in order

#include "neuro/core/endpoint.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Endpoint;
using neuro::core::Message;

namespace {

Message make_msg(std::uint64_t tag, std::initializer_list<std::uint8_t> bytes) {
    Message m{};
    m.tag = tag;
    for (auto b : bytes) {
        m.payload.push_back(static_cast<std::byte>(b));
    }
    return m;
}

}  // namespace

// ---- 1. empty endpoint --------------------------------------------

TEST(endpoint, empty_try_recv_returns_nullopt) {
    Endpoint ep;
    EXPECT_EQ(0u, ep.size());
    EXPECT_FALSE(ep.try_recv().has_value());
}

// ---- 2. send / try_recv FIFO round-trip --------------------------

TEST(endpoint, send_try_recv_fifo) {
    Endpoint ep;
    ep.send(make_msg(1, {0xAA}));
    ep.send(make_msg(2, {0xBB, 0xCC}));
    ep.send(make_msg(3, {}));
    EXPECT_EQ(3u, ep.size());

    auto a = ep.try_recv();
    EXPECT_TRUE(a.has_value());
    EXPECT_EQ(1u, a->tag);
    EXPECT_EQ(static_cast<std::size_t>(1), a->payload.size());
    EXPECT_EQ(static_cast<std::byte>(0xAA), a->payload[0]);

    auto b = ep.try_recv();
    EXPECT_TRUE(b.has_value());
    EXPECT_EQ(2u, b->tag);
    EXPECT_EQ(static_cast<std::size_t>(2), b->payload.size());

    auto c = ep.try_recv();
    EXPECT_TRUE(c.has_value());
    EXPECT_EQ(3u, c->tag);
    EXPECT_TRUE(c->payload.empty());

    EXPECT_FALSE(ep.try_recv().has_value());
}

// ---- 3. size tracking --------------------------------------------

TEST(endpoint, size_tracks_queue_depth) {
    Endpoint ep;
    EXPECT_EQ(0u, ep.size());
    ep.send(make_msg(1, {}));
    EXPECT_EQ(1u, ep.size());
    ep.send(make_msg(2, {}));
    ep.send(make_msg(3, {}));
    EXPECT_EQ(3u, ep.size());
    (void)ep.try_recv();
    EXPECT_EQ(2u, ep.size());
}

// ---- 4. payload bytes survive round-trip --------------------------

TEST(endpoint, payload_round_trip) {
    Endpoint ep;
    Message m{};
    m.tag = 0xDEADBEEFu;
    for (int i = 0; i < 16; ++i) {
        m.payload.push_back(static_cast<std::byte>(i * 7 + 3));
    }
    ep.send(std::move(m));

    auto out = ep.try_recv();
    EXPECT_TRUE(out.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0xDEADBEEFu), out->tag);
    EXPECT_EQ(static_cast<std::size_t>(16), out->payload.size());
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(static_cast<std::byte>(i * 7 + 3), (*out).payload[i]);
    }
}

// ---- 5. mixed-size messages keep order ----------------------------

TEST(endpoint, mixed_size_messages_in_order) {
    Endpoint ep;
    for (int i = 0; i < 5; ++i) {
        ep.send(make_msg(static_cast<std::uint64_t>(i),
                         {static_cast<std::uint8_t>(i),
                          static_cast<std::uint8_t>(i + 1),
                          static_cast<std::uint8_t>(i + 2)}));
    }
    EXPECT_EQ(5u, ep.size());
    for (int i = 0; i < 5; ++i) {
        auto m = ep.try_recv();
        EXPECT_TRUE(m.has_value());
        EXPECT_EQ(static_cast<std::uint64_t>(i), m->tag);
        EXPECT_EQ(static_cast<std::size_t>(3), m->payload.size());
        EXPECT_EQ(static_cast<std::byte>(i),     m->payload[0]);
        EXPECT_EQ(static_cast<std::byte>(i + 1), m->payload[1]);
        EXPECT_EQ(static_cast<std::byte>(i + 2), m->payload[2]);
    }
    EXPECT_FALSE(ep.try_recv().has_value());
}

// ---- 6. non-copyable + non-movable --------------------------------

TEST(endpoint, is_non_copyable) {
    static_assert(!std::is_copy_constructible_v<Endpoint>);
    static_assert(!std::is_copy_assignable_v<Endpoint>);
    static_assert(!std::is_move_constructible_v<Endpoint>);
    static_assert(!std::is_move_assignable_v<Endpoint>);
}

// ---- 7. send/recv from same endpoint is FIFO ---------------------

TEST(endpoint, send_drains_in_fifo) {
    Endpoint ep;
    constexpr int N = 32;
    for (int i = 0; i < N; ++i) {
        Message m{};
        m.tag = static_cast<std::uint64_t>(i);
        ep.send(std::move(m));
    }
    EXPECT_EQ(static_cast<std::size_t>(N), ep.size());
    for (int i = 0; i < N; ++i) {
        auto m = ep.try_recv();
        EXPECT_TRUE(m.has_value());
        EXPECT_EQ(static_cast<std::uint64_t>(i), m->tag);
    }
    EXPECT_EQ(0u, ep.size());
}

RUN_ALL_TESTS()