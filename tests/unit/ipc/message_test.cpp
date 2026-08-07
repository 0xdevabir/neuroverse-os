// tests/unit/ipc/message_test.cpp
//
// Tests for neuro::ipc::Message — the typed IPC envelope.
//
// The end-to-end IPC flow is covered in endpoint_pair_test.cpp and
// core/endpoint_test.cpp. Here we drill into Message / Tag / CapRef:
//
//   - Tag: default, explicit, pack() round-trip, equality, inequality
//   - CapRef: 64-bit handle round-trip
//   - Message: default ctor, Tag ctor, Tag+payload ctor, Tag+CapRef ctor
//   - Message::empty / bytes / with_cap factories
//   - Message::carries_cap() reflects the optional
//   - Message::size() returns payload.size()

#include "neuro/ipc/message.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../test_framework.hpp"

using neuro::ipc::CapRef;
using neuro::ipc::Message;
using neuro::ipc::Tag;

namespace {

std::vector<std::byte> to_bytes(std::initializer_list<std::uint8_t> in) {
    std::vector<std::byte> out;
    for (auto b : in) out.push_back(static_cast<std::byte>(b));
    return out;
}

}  // namespace

// ---- 1. Tag ----------------------------------------------------

TEST(message, tag_default_is_zero) {
    Tag t{};
    EXPECT_EQ(static_cast<std::uint16_t>(0), t.ns);
    EXPECT_EQ(static_cast<std::uint16_t>(0), t.op);
}

TEST(message, tag_explicit_ctor) {
    Tag t(7, 9);
    EXPECT_EQ(static_cast<std::uint16_t>(7), t.ns);
    EXPECT_EQ(static_cast<std::uint16_t>(9), t.op);
}

TEST(message, tag_pack_round_trip) {
    Tag t(0xABCD, 0x1234);
    EXPECT_EQ(static_cast<std::uint32_t>(0xABCD1234u), t.pack());

    // pack always shifts ns high and op low.
    Tag t2(0x0001, 0x0002);
    EXPECT_EQ(static_cast<std::uint32_t>(0x00010002u), t2.pack());

    // zero packs to zero.
    Tag t3(0, 0);
    EXPECT_EQ(static_cast<std::uint32_t>(0), t3.pack());

    // boundary values.
    Tag t4(0xFFFF, 0xFFFF);
    EXPECT_EQ(static_cast<std::uint32_t>(0xFFFFFFFFu), t4.pack());
}

TEST(message, tag_equality) {
    EXPECT_TRUE(Tag(1, 2) == Tag(1, 2));
    EXPECT_FALSE(Tag(1, 2) == Tag(1, 3));
    EXPECT_FALSE(Tag(1, 2) == Tag(0, 2));
}

TEST(message, tag_inequality) {
    EXPECT_FALSE(Tag(1, 2) != Tag(1, 2));
    EXPECT_TRUE(Tag(1, 2)  != Tag(1, 3));
    EXPECT_TRUE(Tag(1, 2)  != Tag(0, 2));
}

// ---- 2. CapRef -------------------------------------------------

TEST(message, capref_round_trip) {
    CapRef c{0xDEADBEEFCAFEBABEULL};
    EXPECT_EQ(static_cast<std::uint64_t>(0xDEADBEEFCAFEBABEULL), c.handle);
}

TEST(message, capref_zero_handle) {
    CapRef c{0};
    EXPECT_EQ(static_cast<std::uint64_t>(0), c.handle);
}

// ---- 3. Message ctor ------------------------------------------

TEST(message, default_ctor) {
    Message m;
    Tag z{};
    EXPECT_TRUE(m.tag == z);
    EXPECT_TRUE(m.payload.empty());
    EXPECT_FALSE(m.carries_cap());
}

TEST(message, tag_ctor_no_payload_no_cap) {
    Message m(Tag(3, 4));
    Tag expected(3, 4);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_TRUE(m.payload.empty());
    EXPECT_FALSE(m.carries_cap());
}

TEST(message, tag_payload_ctor) {
    auto bytes = to_bytes({0xDE, 0xAD, 0xBE, 0xEF});
    Message m(Tag(5, 6), bytes);
    Tag expected(5, 6);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_EQ(static_cast<std::size_t>(4), m.payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE),
              static_cast<std::uint8_t>(m.payload[0]));
    EXPECT_FALSE(m.carries_cap());
}

TEST(message, tag_capref_ctor) {
    Message m(Tag(7, 8), CapRef{42});
    Tag expected(7, 8);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_TRUE(m.payload.empty());
    EXPECT_TRUE(m.carries_cap());
    EXPECT_TRUE(m.cap.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(42), m.cap->handle);
}

// ---- 4. factories ---------------------------------------------

TEST(message, factory_empty) {
    Message m = Message::empty(Tag(9, 10));
    Tag expected(9, 10);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_TRUE(m.payload.empty());
    EXPECT_FALSE(m.carries_cap());
}

TEST(message, factory_bytes) {
    auto bytes = to_bytes({0x42, 0x43});
    Message m = Message::bytes(Tag(1, 2), bytes);
    Tag expected(1, 2);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_EQ(static_cast<std::size_t>(2), m.payload.size());
}

TEST(message, factory_with_cap) {
    Message m = Message::with_cap(Tag(1, 2), CapRef{99});
    Tag expected(1, 2);
    EXPECT_TRUE(m.tag == expected);
    EXPECT_TRUE(m.carries_cap());
    EXPECT_EQ(static_cast<std::uint64_t>(99), m.cap->handle);
}

// ---- 5. helpers ------------------------------------------------

TEST(message, size_returns_payload_size) {
    Message m;
    EXPECT_EQ(static_cast<std::size_t>(0), m.size());

    auto bytes = to_bytes({1, 2, 3, 4, 5});
    Message m2(Tag(1, 2), bytes);
    EXPECT_EQ(static_cast<std::size_t>(5), m2.size());

    auto bytes2 = to_bytes({0xCA, 0xFE});
    Message m3 = Message::bytes(Tag(1, 2), bytes2);
    EXPECT_EQ(static_cast<std::size_t>(2), m3.size());
}

TEST(message, carries_cap_reflects_optional) {
    Message m1;
    EXPECT_FALSE(m1.carries_cap());

    Message m2(Tag(1, 2), CapRef{1});
    EXPECT_TRUE(m2.carries_cap());

    auto bytes = to_bytes({1});
    Message m3(Tag(1, 2), bytes);
    EXPECT_FALSE(m3.carries_cap());
}

// ---- 6. move semantics ----------------------------------------

TEST(message, move_payload) {
    auto bytes = to_bytes({0xA, 0xB, 0xC});
    Message m1(Tag(1, 2), bytes);
    EXPECT_EQ(static_cast<std::size_t>(3), m1.payload.size());

    Message m2 = std::move(m1);
    Tag expected(1, 2);
    EXPECT_TRUE(m2.tag == expected);
    EXPECT_EQ(static_cast<std::size_t>(3), m2.payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xA),
              static_cast<std::uint8_t>(m2.payload[0]));
}

TEST(message, empty_then_assign_payload) {
    Message m = Message::empty(Tag(1, 2));
    EXPECT_TRUE(m.payload.empty());

    auto bytes = to_bytes({0x99, 0xAA});
    m.payload = bytes;
    EXPECT_EQ(static_cast<std::size_t>(2), m.payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x99),
              static_cast<std::uint8_t>(m.payload[0]));
}

RUN_ALL_TESTS()