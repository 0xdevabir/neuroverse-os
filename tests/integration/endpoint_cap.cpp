// tests/integration/endpoint_cap.cpp
//
// Z7.2 — an Endpoint itself, wrapped in a capability, granted to
// another space, then used to send a Message.
//
// Verifies the dual of Z7.1: the capability is to a transport
// primitive (EndpointPair), not to a passive object. Receiving a
// transport cap means "you can talk to this thing", which composes
// naturally with the existing Message-over-IPC test surface.

#include "tests/test_framework.hpp"

#include "neuro/ipc/endpoint_pair.hpp"
#include "neuro/ipc/message.hpp"
#include "neuro/sec/cap_ops.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

using neuro::core::CapRight;
using neuro::ipc::CapRef;
using neuro::ipc::EndpointPair;
using neuro::ipc::Message;
using neuro::ipc::Tag;
using neuro::sec::CapabilitySpace;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;

namespace {

// Well-known object id representing "the transport primitive".
// In a real kernel this would be the kobject id of an Endpoint.
constexpr std::uint64_t TRANSPORT_OBJECT_ID = 0xDEADBEEFULL;

constexpr Tag USE_TRANSPORT{0x000B, 0x0010};

}  // namespace

TEST(endpoint_cap, transport_cap_granted_and_resolved) {
    // A holds a transport primitive (the EndpointPair); A wraps it
    // in a cap, grants the cap to B, and sends the B-side handle
    // over an out-of-band channel. B resolves the cap.
    CapabilitySpace space_a;
    CapabilitySpace space_b;
    CapEpoch        epoch_a;
    CapEpoch        epoch_b;

    auto src_handle = CapOps::mint(space_a, epoch_a,
                                     TRANSPORT_OBJECT_ID,
                                     CapRight::Read | CapRight::Write |
                                     CapRight::Grant, 1);
    EXPECT_TRUE(src_handle != neuro::sec::kInvalidHandle);

    auto granted = CapOps::grant(space_a, space_b, epoch_a,
                                  src_handle, CapRight::None,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    EndpointPair pair;
    auto a = pair.a();
    auto b = pair.b();

    // A sends the B-side transport handle.
    EXPECT_TRUE(a.send_nowait(Message::with_cap(USE_TRANSPORT,
                                                 CapRef{granted.handle})));

    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());
    EXPECT_TRUE(got->cap.has_value());

    // B resolves the cap.
    auto resolved = CapOps::resolve(space_b, epoch_b, got->cap->handle,
                                     CapRight::Read);
    EXPECT_TRUE(resolved.has_value());
    EXPECT_EQ(TRANSPORT_OBJECT_ID, resolved->object_id);
    EXPECT_TRUE(resolved->has(CapRight::Read));
    EXPECT_TRUE(resolved->has(CapRight::Write));
}

TEST(endpoint_cap, cap_rights_gate_resolution) {
    // Only the Read right is granted; B can resolve with Read but
    // not with Write.
    CapabilitySpace space_a;
    CapabilitySpace space_b;
    CapEpoch        epoch_a;
    CapEpoch        epoch_b;

    auto src_handle = CapOps::mint(space_a, epoch_a,
                                     TRANSPORT_OBJECT_ID,
                                     CapRight::Read | CapRight::Write |
                                     CapRight::Grant, 1);
    auto granted = CapOps::grant(space_a, space_b, epoch_a,
                                  src_handle, CapRight::Read,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    EndpointPair pair;
    auto a = pair.a();
    auto b = pair.b();

    EXPECT_TRUE(a.send_nowait(Message::with_cap(USE_TRANSPORT,
                                                 CapRef{granted.handle})));
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());

    auto with_read = CapOps::resolve(space_b, epoch_b, got->cap->handle,
                                      CapRight::Read);
    EXPECT_TRUE(with_read.has_value());

    auto with_write = CapOps::resolve(space_b, epoch_b, got->cap->handle,
                                       CapRight::Write);
    EXPECT_FALSE(with_write.has_value());
}

TEST(endpoint_cap, revoke_invalidates_transport_cap) {
    CapabilitySpace space_a;
    CapabilitySpace space_b;
    CapEpoch        epoch_a;
    CapEpoch        epoch_b;

    auto src_handle = CapOps::mint(space_a, epoch_a,
                                     TRANSPORT_OBJECT_ID,
                                     CapRight::Read | CapRight::Grant, 1);
    auto granted = CapOps::grant(space_a, space_b, epoch_a,
                                  src_handle, CapRight::None,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    EndpointPair pair;
    auto a = pair.a();
    auto b = pair.b();

    EXPECT_TRUE(a.send_nowait(Message::with_cap(USE_TRANSPORT,
                                                 CapRef{granted.handle})));
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());

    // Initially resolvable.
    EXPECT_TRUE(CapOps::resolve(space_b, epoch_b, got->cap->handle,
                                 CapRight::Read).has_value());

    // Revoke B's space.
    CapOps::revoke(space_b, epoch_b);

    EXPECT_FALSE(CapOps::resolve(space_b, epoch_b, got->cap->handle,
                                  CapRight::Read).has_value());
}

RUN_ALL_TESTS()