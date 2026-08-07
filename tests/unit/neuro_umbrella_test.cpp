// tests/unit/neuro_umbrella_test.cpp
//
// Smoke test for include/neuro/neuro.hpp — the umbrella header that
// pulls in every public NeuroLib subsystem. The goal is to catch
// missing subsystem headers or namespace regressions at compile
// time and to verify a small "live" usage call from each subsystem
// so the umbrella is exercised end-to-end.
//
// This is intentionally light: each subsystem is already covered
// by its dedicated unit/integration tests. Here we only verify the
// umbrella compiles and that we can name + invoke a key type from
// each namespace.

#include "neuro/neuro.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "../test_framework.hpp"

// ---- NeuroCore ---------------------------------------------------------

TEST(umbrella, core_capability_round_trip) {
    neuro::core::CapRight r = neuro::core::CapRight::Read
                            | neuro::core::CapRight::Write;
    auto c = neuro::core::Capability::mint(1, r, /*epoch=*/0,
                                            /*generation=*/1);
    EXPECT_TRUE(c.has(neuro::core::CapRight::Read));
    EXPECT_TRUE(c.has(neuro::core::CapRight::Write));
    EXPECT_FALSE(c.has(neuro::core::CapRight::Exec));
}

TEST(umbrella, core_endpoint_send_recv) {
    neuro::core::Endpoint ep;
    neuro::core::Message m{};
    m.tag = 7;
    ep.send(std::move(m));
    EXPECT_EQ(1u, ep.size());
    auto out = ep.try_recv();
    EXPECT_TRUE(out.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(7), out->tag);
}

// ---- NeuroSec ----------------------------------------------------------

TEST(umbrella, sec_cap_ops_namespace_reachable) {
    // Verify a key static method is reachable.
    using O = neuro::sec::CapOps;
    (void)&O::attenuate;
}

// ---- NeuroMem ----------------------------------------------------------

TEST(umbrella, mem_arena_allocate) {
    neuro::mem::Arena arena(4096);
    auto* p = arena.allocate(8);
    EXPECT_TRUE(p != nullptr);
}

TEST(umbrella, mem_pool_allocate) {
    neuro::mem::Pool<std::uint64_t, 4> pool;
    EXPECT_EQ(static_cast<std::size_t>(4), pool.capacity());
    EXPECT_EQ(static_cast<std::size_t>(0), pool.in_use());
    void* p = pool.allocate();
    EXPECT_TRUE(p != nullptr);
    EXPECT_EQ(static_cast<std::size_t>(1), pool.in_use());
}

// ---- NeuroProc / Sched -------------------------------------------------

TEST(umbrella, proc_thread_state_reachable) {
    using S = neuro::proc::ThreadState;
    S s = S::Ready;
    (void)s;
}

TEST(umbrella, sched_scheduler_compiles) {
    // Just reach into the namespace — the work-stealing scheduler
    // spawns threads in its constructor, so don't actually construct.
    using neuro::sched::ws::Scheduler;
    static_assert(std::is_default_constructible_v<Scheduler>);
}

// ---- NeuroIPC / Net ----------------------------------------------------

TEST(umbrella, ipc_message_compile) {
    neuro::ipc::Message m{};
    m.tag = neuro::ipc::Tag{0, 1};
    (void)m;
}

TEST(umbrella, net_channel_send_size) {
    neuro::net::Channel<int> c;
    c.send(42);
    EXPECT_EQ(1u, c.size());
}

// ---- NeuroFS -----------------------------------------------------------

TEST(umbrella, fs_vfs_open_flags) {
    using F = neuro::fs::OpenFlags;
    auto f = F::Read;
    (void)f;
}

// ---- NeuroDev ----------------------------------------------------------

TEST(umbrella, dev_driver_state_reachable) {
    using S = neuro::dev::DriverState;
    S s = S::Registered;
    (void)s;
}

// ---- NeuroUI -----------------------------------------------------------

TEST(umbrella, ui_scene_name) {
    neuro::ui::Scene s("root");
    EXPECT_EQ(std::string("root"), s.name());
}

// ---- NeuroAudio --------------------------------------------------------

TEST(umbrella, audio_buffer_defaults) {
    neuro::audio::Buffer b{};
    EXPECT_EQ(0u, b.frames);
    EXPECT_EQ(2u, b.channels);
}

// ---- NeuroFabric -------------------------------------------------------

TEST(umbrella, fabric_status_reachable) {
    auto s = neuro::fabric::Status::Alive;
    (void)s;
}

// ---- NeuroPkg ----------------------------------------------------------

TEST(umbrella, pkg_digest_aliases) {
    neuro::pkg::Digest d{};
    EXPECT_EQ(static_cast<std::size_t>(64), d.size());
}

TEST(umbrella, pkg_sha3_reachable) {
    // SHA3-512 of empty input == known FIPS-202 test vector:
    //   a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6...
    std::span<const std::byte> empty{};
    auto h = neuro::pkg::sha3::sha3_512(empty);
    EXPECT_EQ(static_cast<std::size_t>(64), h.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xa6),
              static_cast<std::uint8_t>(h[0]));
}

TEST(umbrella, pkg_sha3_256_reachable) {
    std::span<const std::byte> empty{};
    auto h = neuro::pkg::sha3::sha3_256(empty);
    EXPECT_EQ(static_cast<std::size_t>(32), h.size());
    // First byte of the empty-input SHA3-256 digest is 0xa7
    //   (FIPS-202 reference vector).
    EXPECT_EQ(static_cast<std::uint8_t>(0xa7),
              static_cast<std::uint8_t>(h[0]));
}

TEST(umbrella, pkg_store_reachable) {
    // Verify the Store class type is reachable through the umbrella.
    // We can't instantiate it here (the only concrete backend lives
    // in src/pkg/store.cpp as HostStore); we just want to know the
    // type is visible to anyone who includes neuro.hpp.
    using neuro::pkg::Store;
    static_assert(!std::is_default_constructible_v<Store>,
                  "Store is abstract; only concrete backends exist");
}

// ---- NeuroJIT ----------------------------------------------------------

TEST(umbrella, jit_targets_reachable) {
    (void)neuro::jit::Target::X86_64;
    (void)neuro::jit::Target::Host;
}

// ---- NeuroProof --------------------------------------------------------

TEST(umbrella, proof_contract_to_string) {
    neuro::proof::Contract c{};
    c.expects = "x > 0";
    auto s = c.to_string();
    EXPECT_FALSE(s.empty());
    EXPECT_TRUE(s.find("x > 0") != std::string::npos);
}

// ---- NeuroPulse --------------------------------------------------------

TEST(umbrella, pulse_counter_compile) {
    neuro::pulse::Counter c("requests", "count of served requests");
    c.inc();
    EXPECT_EQ(1.0, c.value());
}

// ---- NeuroLearn --------------------------------------------------------

TEST(umbrella, learn_kind_reachable) {
    (void)neuro::learn::Proposal::Kind::SetWorkerCount;
}

// ---- NeuroBridge -------------------------------------------------------

TEST(umbrella, bridge_resolve_error_reachable) {
    // No enum; verify the singleton is reachable.
    auto& b = neuro::bridge::host_bridge();
    EXPECT_EQ(static_cast<std::size_t>(0), b.library_count());
}

// ---- NeuroBoot ---------------------------------------------------------

TEST(umbrella, boot_protocol_kind_reachable) {
    using K = neuro::boot::Segment::Kind;
    (void)K::Text;
    (void)K::Rodata;
    (void)K::Data;
    (void)K::Bss;
}

RUN_ALL_TESTS()
