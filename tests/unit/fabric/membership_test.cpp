// tests/unit/fabric/membership_test.cpp
//
// Tests for the SWIM-style gossip cluster (S1). Covers:
//   - local()/self() configures the local node
//   - start() with initial peers fires on_join for each peer
//   - advance() past ping_timeout moves Alive → Suspect
//   - advance() past 2x ping_timeout moves Suspect → Dead
//   - ping() revives a Suspect/Dead peer (callback fires on_revive)
//   - mark() lets the operator drive transitions
//   - stop() announces Leaving and fires on_leave for self
//   - host_cluster() is a process-wide singleton

#include "neuro/fabric/membership.hpp"

#include <chrono>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::fabric::Cluster;
using neuro::fabric::Callbacks;
using neuro::fabric::Member;
using neuro::fabric::NodeId;
using neuro::fabric::Status;
using neuro::fabric::host_cluster;
using neuro::fabric::make_test_cluster;

namespace {

// Helper: build a fresh Member with explicit fields.
Member make_member(NodeId id, const std::string& addr) {
    Member m;
    m.id     = id;
    m.addr   = addr;
    m.status = Status::Alive;
    return m;
}

// Helper: short defaults for tests.
std::unique_ptr<Cluster> fresh() {
    auto c = make_test_cluster();
    c->configure(std::chrono::milliseconds{100},
                 std::chrono::milliseconds{500});
    return c;
}

}  // namespace

// ---- 1. local()/self() round trip -------------------------------------

TEST(fabric, local_self_round_trip) {
    auto c = fresh();
    c->local(/*id=*/42, "127.0.0.1:7000");
    EXPECT_EQ(42u, c->self());
}

TEST(fabric, local_id_zero_picks_default) {
    auto c = fresh();
    c->local(0, "");
    EXPECT_NE(0u, c->self());  // any non-zero default
}

// ---- 2. start() with peers fires on_join ------------------------------

TEST(fabric, start_with_peers_fires_on_join) {
    auto c = fresh();
    c->local(1, "127.0.0.1:7001");

    std::vector<NodeId> joined;
    Callbacks cb;
    cb.on_join = [&joined](const Member& m) {
        joined.push_back(m.id);
    };
    c->start(cb, {
        make_member(2, "127.0.0.1:7002"),
        make_member(3, "127.0.0.1:7003"),
    });

    EXPECT_EQ(2u, joined.size());
    // Members include ourselves + the two peers.
    auto mems = c->members();
    EXPECT_EQ(3u, mems.size());
}

// ---- 3. Members snapshot includes self + peers -----------------------

TEST(fabric, members_snapshot_includes_self_and_peers) {
    auto c = fresh();
    c->local(1, "");
    c->start({}, {make_member(2, ""), make_member(3, "")});
    auto mems = c->members();
    EXPECT_EQ(3u, mems.size());
    bool found1 = false, found2 = false, found3 = false;
    for (auto& m : mems) {
        if (m.id == 1) found1 = true;
        if (m.id == 2) found2 = true;
        if (m.id == 3) found3 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
    EXPECT_TRUE(found3);
}

// ---- 4. advance() past ping_timeout moves Alive → Suspect -------------

TEST(fabric, advance_past_timeout_marks_suspect) {
    auto c = fresh();
    c->local(1, "");
    std::vector<NodeId> suspects;
    Callbacks cb;
    cb.on_suspect = [&suspects](const Member& m) {
        suspects.push_back(m.id);
    };
    c->start(cb, {make_member(2, "")});

    // advance(0) then advance(700ms) — past the 500ms timeout.
    c->advance(std::chrono::milliseconds{0});
    c->advance(std::chrono::milliseconds{700});
    EXPECT_EQ(1u, suspects.size());
    EXPECT_EQ(2u, suspects.front());
}

// ---- 5. advance() past 2x timeout moves Suspect → Dead ---------------

TEST(fabric, advance_past_double_timeout_marks_dead) {
    auto c = fresh();
    c->local(1, "");
    std::vector<NodeId> left;
    Callbacks cb;
    cb.on_leave = [&left](const Member& m) {
        left.push_back(m.id);
    };
    c->start(cb, {make_member(2, "")});

    // First advance → Suspect; second advance → Dead.
    c->advance(std::chrono::milliseconds{700});
    c->advance(std::chrono::milliseconds{700});
    EXPECT_EQ(1u, left.size());
    EXPECT_EQ(2u, left.front());
}

// ---- 6. ping() revives a Suspect peer --------------------------------

TEST(fabric, ping_revives_suspect_peer) {
    auto c = fresh();
    c->local(1, "");
    std::vector<NodeId> revived;
    std::vector<NodeId> suspects;
    Callbacks cb;
    cb.on_suspect = [&suspects](const Member& m) { suspects.push_back(m.id); };
    cb.on_revive  = [&revived](const Member& m)  { revived.push_back(m.id);  };
    c->start(cb, {make_member(2, "")});

    c->advance(std::chrono::milliseconds{700});  // Suspect
    EXPECT_EQ(1u, suspects.size());

    EXPECT_TRUE(c->ping(2));
    EXPECT_EQ(1u, revived.size());
    EXPECT_EQ(2u, revived.front());
    // The peer is now Alive again.
    auto mems = c->members();
    for (auto& m : mems) {
        if (m.id == 2) {
            EXPECT_EQ(Status::Alive, m.status);
        }
    }
}

// ---- 7. mark() drives status transitions ------------------------------

TEST(fabric, mark_drives_transitions) {
    auto c = fresh();
    c->local(1, "");
    std::vector<NodeId> left;
    Callbacks cb;
    cb.on_leave = [&left](const Member& m) { left.push_back(m.id); };
    c->start(cb, {make_member(2, "")});
    c->mark(2, Status::Dead);
    EXPECT_EQ(1u, left.size());
    EXPECT_EQ(2u, left.front());
}

// ---- 8. stop() announces Leaving and fires on_leave for self ---------

TEST(fabric, stop_announces_leaving) {
    auto c = fresh();
    c->local(1, "");
    std::vector<NodeId> left;
    Callbacks cb;
    cb.on_leave = [&left](const Member& m) { left.push_back(m.id); };
    c->start(cb, {make_member(2, "")});
    c->stop();
    EXPECT_EQ(1u, left.size());
    EXPECT_EQ(1u, left.front());  // self
}

// ---- 9. Singleton ----------------------------------------------------

TEST(fabric, host_cluster_is_singleton) {
    auto& a = host_cluster();
    auto& b = host_cluster();
    EXPECT_EQ(&a, &b);
}

// ---- 10. ping() unknown peer returns false ----------------------------

TEST(fabric, ping_unknown_returns_false) {
    auto c = fresh();
    c->local(1, "");
    c->start({}, {});
    EXPECT_FALSE(c->ping(999));
}

RUN_ALL_TESTS()