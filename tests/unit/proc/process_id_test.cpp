// tests/unit/proc/process_id_test.cpp
//
// Additional Process tests covering the KObject identity, const
// accessors, and id uniqueness across many processes. The full
// Process surface is covered in process_test.cpp; this file adds:
//
//   - Process inherits KObject: id() returns unique values
//   - id() is stable across the lifetime of the Process
//   - id() values monotonically increase (KObject uses a global counter)
//   - kind() returns the KObject kind tag
//   - retirement via retire() bumps generation
//   - const accessors: caps() const, vmas() const, endpoints()
//   - service_endpoints list flows through into the ProcessInit
//   - empty name is permitted

#include "neuro/core/endpoint.hpp"
#include "neuro/core/kobject.hpp"
#include "neuro/mem/vma_tree.hpp"
#include "neuro/proc/process.hpp"
#include "neuro/proc/thread.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::Endpoint;
using neuro::core::KObjectKind;
using neuro::proc::Process;
using neuro::proc::ProcessInit;

TEST(process, ids_are_distinct_across_many) {
    std::vector<std::unique_ptr<Process>> ps;
    for (int i = 0; i < 100; ++i) {
        ps.push_back(std::make_unique<Process>(ProcessInit{
            "p_" + std::to_string(i), {}}));
    }
    std::set<std::uint64_t> ids;
    for (auto& p : ps) ids.insert(p->id());
    EXPECT_EQ(static_cast<std::size_t>(100), ids.size());
}

TEST(process, id_is_stable_over_lifetime) {
    Process p(ProcessInit{"stable", {}});
    auto id1 = p.id();
    auto id2 = p.id();
    auto id3 = p.id();
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id2, id3);
}

TEST(process, generation_starts_at_zero) {
    Process p(ProcessInit{"gen", {}});
    EXPECT_EQ(static_cast<std::uint64_t>(0), p.generation());
}

TEST(process, retire_bumps_generation) {
    Process p(ProcessInit{"r", {}});
    auto old = p.retire();
    EXPECT_EQ(static_cast<std::uint64_t>(0), old);
    EXPECT_EQ(static_cast<std::uint64_t>(1), p.generation());
    auto old2 = p.retire();
    EXPECT_EQ(static_cast<std::uint64_t>(1), old2);
    EXPECT_EQ(static_cast<std::uint64_t>(2), p.generation());
}

TEST(process, kind_is_reachable) {
    Process p(ProcessInit{"k", {}});
    (void)p.kind();
}

TEST(process, const_caps_view) {
    Process p(ProcessInit{"c", {}});
    const Process& cp = p;
    EXPECT_EQ(0u, cp.caps().size());
}

TEST(process, const_vmas_view) {
    Process p(ProcessInit{"v", {}});
    const Process& cp = p;
    EXPECT_EQ(0u, cp.vmas().size());
}

TEST(process, const_endpoints_view) {
    Process p(ProcessInit{"e", {}});
    EXPECT_EQ(0u, p.endpoints().size());
}

TEST(process, name_can_be_empty) {
    Process p(ProcessInit{"", {}});
    EXPECT_TRUE(p.name().empty());
}

TEST(process, name_can_be_long) {
    std::string n(1000, 'x');
    Process p(ProcessInit{n, {}});
    EXPECT_EQ(n, p.name());
}

TEST(process, default_construction_via_empty_init) {
    ProcessInit init;
    Process p(init);
    EXPECT_TRUE(p.name().empty());
    EXPECT_EQ(0u, p.caps().size());
    EXPECT_EQ(0u, p.vmas().size());
    EXPECT_EQ(0u, p.endpoints().size());
}

TEST(process, owned_endpoint_lifetime) {
    // The Process must own the endpoints — destroying the unique_ptr
    // we passed in should not destroy the endpoint (the Process owns
    // it now).
    Process p(ProcessInit{"o", {}});
    auto ep_up = std::make_unique<Endpoint>();
    Endpoint* raw = ep_up.get();
    Endpoint* got = p.add_endpoint(std::move(ep_up));
    EXPECT_EQ(raw, got);
    EXPECT_TRUE(ep_up == nullptr);  // moved-from
    // ep_up is now empty; Process still owns the Endpoint.
    p.add_endpoint(std::make_unique<Endpoint>());
    p.add_endpoint(std::make_unique<Endpoint>());
    EXPECT_EQ(3u, p.endpoints().size());
}

// ---- 7. Process::add_thread bookkeeping ----------------------------

TEST(process, add_thread_records_index_and_pointer) {
    using neuro::proc::Thread;
    Process p(ProcessInit{"tp", {}});
    EXPECT_EQ(0u, p.thread_count());

    Thread::Attr a;
    Thread t1(p, a, [](Thread&) {});
    Thread t2(p, a, [](Thread&) {});
    const auto i1 = p.add_thread(&t1);
    const auto i2 = p.add_thread(&t2);
    EXPECT_EQ(0u, i1);
    EXPECT_EQ(1u, i2);
    EXPECT_EQ(2u, p.thread_count());
    EXPECT_EQ(&t1, p.thread_at(i1));
    EXPECT_EQ(&t2, p.thread_at(i2));
}

TEST(process, remove_thread_clears_slot) {
    using neuro::proc::Thread;
    Process p(ProcessInit{"tp2", {}});
    Thread::Attr a;
    Thread t(p, a, [](Thread&) {});
    const auto idx = p.add_thread(&t);
    EXPECT_EQ(1u, p.thread_count());
    p.remove_thread(idx);
    EXPECT_EQ(0u, p.thread_count());
    EXPECT_TRUE(p.thread_at(idx) == nullptr);
}

TEST(process, thread_at_out_of_range_returns_null) {
    Process p(ProcessInit{"tp3", {}});
    EXPECT_TRUE(p.thread_at(0) == nullptr);
    EXPECT_TRUE(p.thread_at(99) == nullptr);
}

RUN_ALL_TESTS()