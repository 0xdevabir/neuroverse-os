// tests/integration/fabric_processes.cpp
//
// Z7.11 — fabric ping across processes.
//
// Two Cluster instances ("process A" and "process B") are created
// from the test factory; each calls `local()` to set its own id,
// then `start()` with the other as an initial peer. From there:
//
//   1. A direct `ping()` from A's cluster revives B's status if
//      it has been timed-out.
//   2. `mark()` lets us inject Suspect / Dead transitions.
//   3. `advance(dt)` walks the gossip clock forward so timeouts
//      fire deterministically without a background thread.
//   4. The `on_revive` / `on_suspect` / `on_leave` callbacks fire
//      on every transition.
//   5. `members()` reflects the latest state of every peer.
//   6. `stop()` announces `Left` and tears the cluster down.
//
// The host scaffold does not actually exchange UDP packets between
// the two clusters — they are independent in-process state machines.
// What we are validating is the membership *state machine* that the
// kernel implementation will inherit verbatim.

#include "tests/test_framework.hpp"

#include "neuro/fabric/membership.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using neuro::fabric::Callbacks;
using neuro::fabric::Cluster;
using neuro::fabric::make_test_cluster;
using neuro::fabric::Member;
using neuro::fabric::NodeId;
using neuro::fabric::Status;

namespace {

// Build a fresh cluster with probe_interval=100ms, ping_timeout=500ms.
// The exact values matter for the timeout-driven tests below.
std::unique_ptr<Cluster> build_process(NodeId id, const std::string& addr) {
    auto c = make_test_cluster();
    c->configure(std::chrono::milliseconds(100),
                 std::chrono::milliseconds(500));
    c->local(id, addr);
    return c;
}

// Find a member by id in a snapshot.
const Member* find_member(const std::vector<Member>& ms, NodeId id) {
    for (const auto& m : ms) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

}  // namespace

TEST(fabric_processes, two_clusters_see_each_other_after_start) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer;
    b_peer.id   = 2;
    b_peer.addr = "10.0.0.2:7000";

    Member a_peer;
    a_peer.id   = 1;
    a_peer.addr = "10.0.0.1:7000";

    a->start(Callbacks{}, {b_peer});
    b->start(Callbacks{}, {a_peer});

    EXPECT_EQ(static_cast<NodeId>(1), a->self());
    EXPECT_EQ(static_cast<NodeId>(2), b->self());

    auto a_view = a->members();
    auto b_view = b->members();
    EXPECT_EQ(static_cast<std::size_t>(2), a_view.size());
    EXPECT_EQ(static_cast<std::size_t>(2), b_view.size());

    const Member* a_sees_b = find_member(a_view, 2);
    EXPECT_TRUE(a_sees_b != nullptr);
    EXPECT_EQ(Status::Alive, a_sees_b->status);

    const Member* b_sees_a = find_member(b_view, 1);
    EXPECT_TRUE(b_sees_a != nullptr);
    EXPECT_EQ(Status::Alive, b_sees_a->status);
}

TEST(fabric_processes, direct_ping_keeps_peer_alive) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    a->start(Callbacks{}, {b_peer});
    b->start(Callbacks{}, {a_peer});

    // Mark B as Suspect from A's side.
    a->mark(2, Status::Suspect);
    auto view = a->members();
    EXPECT_EQ(Status::Suspect, find_member(view, 2)->status);

    // A direct ping revives it.
    EXPECT_TRUE(a->ping(2));
    view = a->members();
    EXPECT_EQ(Status::Alive, find_member(view, 2)->status);
}

TEST(fabric_processes, mark_suspect_fires_on_suspect_callback) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    int suspect_count = 0;
    NodeId suspect_id = 0;
    Callbacks cb;
    cb.on_suspect = [&](const Member& m) {
        suspect_count++;
        suspect_id = m.id;
    };

    a->start(cb, {b_peer});
    b->start(Callbacks{}, {a_peer});

    a->mark(2, Status::Suspect);
    EXPECT_EQ(1, suspect_count);
    EXPECT_EQ(static_cast<NodeId>(2), suspect_id);
}

TEST(fabric_processes, mark_dead_fires_on_leave_callback) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    int leave_count = 0;
    NodeId leave_id = 0;
    Callbacks cb;
    cb.on_leave = [&](const Member& m) {
        leave_count++;
        leave_id = m.id;
    };

    a->start(cb, {b_peer});
    b->start(Callbacks{}, {a_peer});

    a->mark(2, Status::Dead);
    EXPECT_EQ(1, leave_count);
    EXPECT_EQ(static_cast<NodeId>(2), leave_id);

    auto view = a->members();
    EXPECT_EQ(Status::Dead, find_member(view, 2)->status);
}

TEST(fabric_processes, revive_incarnates_and_fires_on_revive) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    int revive_count = 0;
    Callbacks cb;
    cb.on_revive = [&](const Member&) { revive_count++; };

    a->start(cb, {b_peer});
    b->start(Callbacks{}, {a_peer});

    auto view = a->members();
    std::uint64_t base_incarnation =
        find_member(view, 2)->incarnation;

    // Mark Suspect, then revive via ping.
    a->mark(2, Status::Suspect);
    a->ping(2);
    EXPECT_EQ(1, revive_count);

    view = a->members();
    EXPECT_EQ(base_incarnation + 1, find_member(view, 2)->incarnation);
    EXPECT_EQ(Status::Alive, find_member(view, 2)->status);
}

TEST(fabric_processes, advance_clock_drives_alive_to_suspect) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    int suspect_count = 0;
    Callbacks cb;
    cb.on_suspect = [&](const Member&) { suspect_count++; };

    a->start(cb, {b_peer});
    b->start(Callbacks{}, {a_peer});

    // Advance the clock past `ping_timeout` (500ms) but not yet
    // twice it. The peer should transition to Suspect.
    a->advance(std::chrono::milliseconds(600));
    EXPECT_EQ(1, suspect_count);

    auto view = a->members();
    EXPECT_EQ(Status::Suspect, find_member(view, 2)->status);
}

TEST(fabric_processes, advance_clock_drives_suspect_to_dead) {
    auto a = build_process(1, "10.0.0.1:7000");
    auto b = build_process(2, "10.0.0.2:7000");

    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    Member a_peer; a_peer.id = 1; a_peer.addr = "10.0.0.1:7000";

    int leave_count = 0;
    Callbacks cb;
    cb.on_leave = [&](const Member&) { leave_count++; };

    a->start(cb, {b_peer});
    b->start(Callbacks{}, {a_peer});

    // First step drives Alive -> Suspect (>= 1 * ping_timeout).
    a->advance(std::chrono::milliseconds(600));
    EXPECT_EQ(0, leave_count);

    // Second step crosses 2 * ping_timeout, so Suspect -> Dead.
    a->advance(std::chrono::milliseconds(600));
    EXPECT_EQ(1, leave_count);

    auto view = a->members();
    EXPECT_EQ(Status::Dead, find_member(view, 2)->status);
}

TEST(fabric_processes, ping_unknown_id_returns_false) {
    auto a = build_process(1, "10.0.0.1:7000");
    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    a->start(Callbacks{}, {b_peer});

    EXPECT_FALSE(a->ping(999));
    EXPECT_TRUE(a->ping(2));
}

TEST(fabric_processes, stop_announces_left_and_idempotent) {
    auto a = build_process(1, "10.0.0.1:7000");
    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";

    int leave_count = 0;
    Callbacks cb;
    cb.on_leave = [&](const Member& m) {
        if (m.id == 1) leave_count++;
    };

    a->start(cb, {b_peer});
    a->stop();
    EXPECT_EQ(1, leave_count);

    // Second stop is a no-op (no further callback).
    a->stop();
    EXPECT_EQ(1, leave_count);
}

TEST(fabric_processes, mark_unknown_id_is_silent) {
    auto a = build_process(1, "10.0.0.1:7000");
    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    a->start(Callbacks{}, {b_peer});

    // Should not throw, should not crash; the unknown id is simply
    // ignored.
    a->mark(999, Status::Suspect);
    EXPECT_EQ(static_cast<std::size_t>(2), a->members().size());
}

TEST(fabric_processes, members_snapshot_is_independent_copy) {
    auto a = build_process(1, "10.0.0.1:7000");
    Member b_peer; b_peer.id = 2; b_peer.addr = "10.0.0.2:7000";
    a->start(Callbacks{}, {b_peer});

    auto view1 = a->members();
    EXPECT_EQ(static_cast<std::size_t>(2), view1.size());

    // Mutating the snapshot must not affect the cluster.
    view1[0].status = Status::Dead;

    auto view2 = a->members();
    EXPECT_EQ(Status::Alive, find_member(view2, 2)->status);
}

RUN_ALL_TESTS()
