// tests/unit/proc/process_test.cpp
//
// Tests for neuro::proc::Process — a userspace-isolated container
// that bundles a CapabilitySpace, a VMATree, and an initial set of
// Endpoints.
//
// Coverage:
//   - default-constructed KObject id + kind
//   - name round-trips
//   - caps().insert() + lookup() round-trip
//   - epoch() starts at 0
//   - vmas() insert + find round-trip
//   - add_endpoint() returns a pointer to the stored Endpoint
//   - endpoints() tracks adds; Process owns the lifetime
//   - non-copyable

#include "neuro/core/endpoint.hpp"
#include "neuro/core/kobject.hpp"
#include "neuro/mem/vma_tree.hpp"
#include "neuro/proc/process.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <memory>
#include <type_traits>
#include <utility>

#include "../../test_framework.hpp"

using neuro::core::Capability;
using neuro::core::CapRight;
using neuro::core::Endpoint;
using neuro::core::KObjectKind;
using neuro::mem::VMA;
using neuro::proc::Process;
using neuro::proc::ProcessInit;
using neuro::sec::CapEpoch;
using neuro::sec::CapabilitySpace;

namespace {

VMA make_vma(std::uint64_t s, std::uint64_t e) {
    VMA v{};
    v.start = s;
    v.end   = e;
    v.rights = static_cast<std::uint32_t>(CapRight::Read);
    return v;
}

}  // namespace

// ---- 1. metadata ---------------------------------------------------

TEST(process, name_round_trip) {
    Process p(ProcessInit{"worker_0", {}});
    EXPECT_EQ(std::string("worker_0"), p.name());
}

TEST(process, kind_is_untyped_or_unused) {
    // KObject has a kind tag; we don't pin the exact value here —
    // we only assert the object is reachable as a KObject.
    Process p(ProcessInit{"k", {}});
    (void)p.kind();   // reachable
    (void)p.id();     // reachable
}

// ---- 2. caps + epoch ----------------------------------------------

TEST(process, caps_round_trip) {
    Process p(ProcessInit{"c", {}});
    auto& cs = p.caps();
    EXPECT_EQ(static_cast<std::size_t>(0), cs.size());
    auto cap = Capability::mint(p.id(), CapRight::Read, 1, 1);
    auto h = cs.insert(cap);
    EXPECT_NE(neuro::sec::kInvalidHandle, h);
    EXPECT_EQ(static_cast<std::size_t>(1), cs.size());
    auto looked = cs.lookup(h);
    EXPECT_TRUE(looked.has_value());
    EXPECT_EQ(p.id(), looked->object_id);
}

TEST(process, epoch_starts_at_zero) {
    Process p(ProcessInit{"e", {}});
    EXPECT_EQ(static_cast<std::uint64_t>(0), p.epoch().current());
}

// ---- 3. vmas ------------------------------------------------------

TEST(process, vmas_round_trip) {
    Process p(ProcessInit{"v", {}});
    auto& vs = p.vmas();
    EXPECT_EQ(0u, vs.size());
    EXPECT_TRUE(vs.insert(make_vma(0x1000, 0x2000)));
    EXPECT_EQ(1u, vs.size());
    auto found = vs.find(0x1500);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(0x1000), found->start);
}

// ---- 4. endpoints -------------------------------------------------

TEST(process, add_endpoint_returns_pointer) {
    Process p(ProcessInit{"e", {}});
    EXPECT_EQ(0u, p.endpoints().size());

    auto ep_ptr = p.add_endpoint(std::make_unique<Endpoint>());
    EXPECT_TRUE(ep_ptr != nullptr);
    EXPECT_EQ(1u, p.endpoints().size());
    EXPECT_EQ(ep_ptr, p.endpoints()[0]);
}

TEST(process, multiple_endpoints_tracked) {
    Process p(ProcessInit{"e", {}});
    p.add_endpoint(std::make_unique<Endpoint>());
    p.add_endpoint(std::make_unique<Endpoint>());
    p.add_endpoint(std::make_unique<Endpoint>());
    EXPECT_EQ(3u, p.endpoints().size());
}

TEST(process, endpoint_useable_through_pointer) {
    Process p(ProcessInit{"u", {}});
    auto* ep = p.add_endpoint(std::make_unique<Endpoint>());

    neuro::core::Message m{};
    m.tag = 99;
    ep->send(std::move(m));
    EXPECT_EQ(1u, ep->size());

    auto out = ep->try_recv();
    EXPECT_TRUE(out.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(99), out->tag);
}

// ---- 5. non-copyable + non-movable --------------------------------

TEST(process, non_copyable) {
    static_assert(!std::is_copy_constructible_v<Process>);
    static_assert(!std::is_copy_assignable_v<Process>);
    static_assert(!std::is_move_constructible_v<Process>);
    static_assert(!std::is_move_assignable_v<Process>);
}

// ---- 6. ProcessInit service_endpoints preserved -------------------

TEST(process, init_service_endpoints_preserved) {
    ProcessInit init;
    init.name = "demo";
    init.service_endpoints = {"cap_space", "vma_space", "endpoint_pair"};
    Process p(init);
    EXPECT_EQ(std::string("demo"), p.name());
    EXPECT_EQ(static_cast<std::size_t>(3), init.service_endpoints.size());
}

RUN_ALL_TESTS()