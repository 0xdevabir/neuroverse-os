// tests/integration/cap_ipc.cpp
//
// Z7.1 — capability passed over IPC.
//
// Demonstrates the standard microkernel pattern:
//   1. Process A mints a capability for an object.
//   2. A grants the cap into B's space (so B can resolve it).
//   3. A wraps the B-side handle in an ipc::Message.
//   4. A sends the message to B over an EndpointPair::Side.
//   5. B extracts the CapRef and resolves it through its OWN
//      CapabilitySpace.
//   6. B holds the same object_id + rights after the round-trip.
//
// This composes Sec (cap mint/grant/resolve) and IPC (endpoint +
// Message::with_cap) end-to-end on the host scaffold.

#include "tests/test_framework.hpp"

#include "neuro/ipc/endpoint_pair.hpp"
#include "neuro/ipc/message.hpp"
#include "neuro/sec/cap_ops.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <chrono>
#include <cstdint>

using neuro::core::CapRight;
using neuro::ipc::CapRef;
using neuro::ipc::EndpointPair;
using neuro::ipc::Message;
using neuro::ipc::Tag;
using neuro::sec::CapabilitySpace;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;

namespace {

constexpr Tag CAP_HELLO{0x000A, 0x0001};

// Mocked object id — would be a kobject id in the real kernel.
constexpr std::uint64_t OBJECT_ID = 0xABCD1234ULL;

}  // namespace

TEST(cap_ipc, mint_send_grant_resolve_roundtrip) {
    CapabilitySpace src_space;
    CapabilitySpace dst_space;
    CapEpoch        src_epoch;
    CapEpoch        dst_epoch;

    // 1. A mints a capability for the object.
    auto src_handle = CapOps::mint(src_space, src_epoch, OBJECT_ID,
                                   CapRight::Read | CapRight::Write |
                                   CapRight::Grant,
                                   /*generation=*/1);
    EXPECT_TRUE(src_handle != neuro::sec::kInvalidHandle);

    // 2. A grants the cap into B's space.
    auto granted = CapOps::grant(src_space, dst_space, src_epoch,
                                  src_handle, CapRight::None,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    // The source handle is consumed.
    EXPECT_FALSE(src_space.contains(src_handle));

    // 3. A sends the B-side handle to B over the EndpointPair.
    EndpointPair pair;
    auto a = pair.a();
    EXPECT_TRUE(a.send_nowait(Message::with_cap(CAP_HELLO,
                                                 CapRef{granted.handle})));

    // 4. B receives the message and extracts the CapRef.
    auto b = pair.b();
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());
    EXPECT_TRUE(got->carries_cap());
    EXPECT_EQ(CAP_HELLO, got->tag);
    EXPECT_TRUE(got->cap.has_value());
    auto dst_handle = got->cap->handle;
    EXPECT_EQ(granted.handle, dst_handle);

    // 5. B resolves the handle through its own space + epoch.
    auto resolved = CapOps::resolve(dst_space, dst_epoch, dst_handle,
                                     CapRight::Read);
    EXPECT_TRUE(resolved.has_value());
    EXPECT_EQ(OBJECT_ID, resolved->object_id);
    EXPECT_TRUE(resolved->has(CapRight::Read));
    EXPECT_TRUE(resolved->has(CapRight::Write));
    EXPECT_TRUE(resolved->has(CapRight::Grant));

    // Attenuation works after the round-trip.
    auto narrow = CapOps::attenuate(*resolved, CapRight::Read);
    EXPECT_TRUE(narrow.has_value());
    EXPECT_TRUE(narrow->has(CapRight::Read));
    EXPECT_FALSE(narrow->has(CapRight::Write));
}

TEST(cap_ipc, receiver_cannot_resolve_without_grant) {
    // B receives a cap handle from A but never had the cap granted
    // into B's space. Resolution must fail.
    CapabilitySpace src_space;
    CapabilitySpace dst_space;
    CapEpoch        src_epoch;
    CapEpoch        dst_epoch;

    auto src_handle = CapOps::mint(src_space, src_epoch, OBJECT_ID,
                                    CapRight::Read, 1);

    EndpointPair pair;
    auto a = pair.a();
    EXPECT_TRUE(a.send_nowait(Message::with_cap(CAP_HELLO,
                                                 CapRef{src_handle})));

    auto b = pair.b();
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());
    EXPECT_TRUE(got->cap.has_value());

    // dst_space is empty — resolve must return nullopt.
    auto bogus = CapOps::resolve(dst_space, dst_epoch, got->cap->handle,
                                  CapRight::Read);
    EXPECT_FALSE(bogus.has_value());
}

TEST(cap_ipc, grant_with_attenuation_strips_rights) {
    CapabilitySpace src_space;
    CapabilitySpace dst_space;
    CapEpoch        src_epoch;
    CapEpoch        dst_epoch;

    auto src_handle = CapOps::mint(src_space, src_epoch, OBJECT_ID,
                                    CapRight::Read | CapRight::Write |
                                    CapRight::Grant, 1);

    // Grant only the Read right.
    auto granted = CapOps::grant(src_space, dst_space, src_epoch,
                                  src_handle, CapRight::Read,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    EndpointPair pair;
    auto a = pair.a();
    EXPECT_TRUE(a.send_nowait(Message::with_cap(CAP_HELLO,
                                                 CapRef{granted.handle})));

    auto b = pair.b();
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());

    // Resolve with Read — succeeds.
    auto r1 = CapOps::resolve(dst_space, dst_epoch, got->cap->handle,
                               CapRight::Read);
    EXPECT_TRUE(r1.has_value());

    // Resolve with Write — fails (was attenuated away).
    auto r2 = CapOps::resolve(dst_space, dst_epoch, got->cap->handle,
                               CapRight::Write);
    EXPECT_FALSE(r2.has_value());
}

TEST(cap_ipc, revoke_invalidates_handed_over_cap) {
    CapabilitySpace src_space;
    CapabilitySpace dst_space;
    CapEpoch        src_epoch;
    CapEpoch        dst_epoch;

    auto src_handle = CapOps::mint(src_space, src_epoch, OBJECT_ID,
                                    CapRight::Read | CapRight::Grant, 1);
    auto granted = CapOps::grant(src_space, dst_space, src_epoch,
                                  src_handle, CapRight::None,
                                  /*take=*/true);
    EXPECT_TRUE(granted.ok);

    // Hand the cap over via IPC.
    EndpointPair pair;
    auto a = pair.a();
    EXPECT_TRUE(a.send_nowait(Message::with_cap(CAP_HELLO,
                                                 CapRef{granted.handle})));

    auto b = pair.b();
    auto got = b.try_recv_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(got.has_value());

    // Initially resolvable.
    auto before = CapOps::resolve(dst_space, dst_epoch, got->cap->handle,
                                   CapRight::Read);
    EXPECT_TRUE(before.has_value());

    // Revoke B's space — every cap minted at the current epoch is
    // now invalid.
    CapOps::revoke(dst_space, dst_epoch);

    // Now unresolvable.
    auto after = CapOps::resolve(dst_space, dst_epoch, got->cap->handle,
                                  CapRight::Read);
    EXPECT_FALSE(after.has_value());
}

RUN_ALL_TESTS()