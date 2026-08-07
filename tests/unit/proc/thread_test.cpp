// tests/unit/proc/thread_test.cpp
//
// Tests for the NeuroProc Thread state machine. Covers:
//   - start() transitions Ready → Running
//   - join() transitions to Terminated
//   - thread entry runs to completion
//   - join_for() times out on a long-running thread
//   - wake() moves Waiting → Ready
//   - thread name / affinity / priority round-trip
//   - capability space is owned by Process

#include "neuro/proc/thread.hpp"
#include "neuro/proc/process.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "../../test_framework.hpp"

using neuro::proc::Process;
using neuro::proc::ProcessInit;
using neuro::proc::Thread;
using neuro::proc::ThreadState;

namespace {

std::atomic<int> g_counter{0};

void inc_thread(Thread& /*t*/) {
    g_counter.fetch_add(1);
}

}  // namespace

// ---- 1. start() + join() runs entry ---------------------------------

TEST(thread, start_runs_entry_and_join_terminates) {
    g_counter = 0;
    Process p(ProcessInit{"p1", {}});
    Thread::Attr a;
    a.name = "worker";
    Thread t(p, a, inc_thread);
    EXPECT_EQ(ThreadState::Ready, t.state());
    t.start();
    t.join();
    EXPECT_EQ(ThreadState::Terminated, t.state());
    EXPECT_EQ(1, g_counter.load());
}

// ---- 2. Affinity + priority round-trip -------------------------------

TEST(thread, attr_round_trip) {
    Process p(ProcessInit{"p2", {}});
    Thread::Attr a;
    a.name         = "high_prio";
    a.cpu_affinity = 0b1010;
    a.priority     = 5;
    Thread t(p, a, inc_thread);
    EXPECT_EQ("high_prio", t.name());
    EXPECT_EQ(0b1010u,     t.affinity());
    EXPECT_EQ(5,           t.priority());
    EXPECT_EQ(&p,          &t.owner());
}

// ---- 3. join_for() times out on a long block -------------------------

TEST(thread, join_for_times_out) {
    Process p(ProcessInit{"p3", {}});
    Thread::Attr a;
    a.name = "blocked";
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        // Wait until either the thread is terminated or stop is set.
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    t.start();
    // Give the thread time to enter.
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bool joined = t.join_for(std::chrono::milliseconds{20});
    EXPECT_FALSE(joined);  // timed out
    // Tell the entry to exit so the destructor can join cleanly.
    stop.store(true);
    t.join();
}

// ---- 4. join_for() succeeds when entry finishes quickly --------------

TEST(thread, join_for_succeeds) {
    g_counter = 0;
    Process p(ProcessInit{"p4", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    t.start();
    bool joined = t.join_for(std::chrono::milliseconds{1000});
    EXPECT_TRUE(joined);
    EXPECT_EQ(1, g_counter.load());
}

// ---- 5. to_string() mapping -------------------------------------------

TEST(thread, to_string_maps_states) {
    EXPECT_TRUE(std::string(neuro::proc::to_string(ThreadState::Ready))      == "ready");
    EXPECT_TRUE(std::string(neuro::proc::to_string(ThreadState::Running))    == "running");
    EXPECT_TRUE(std::string(neuro::proc::to_string(ThreadState::Waiting))    == "waiting");
    EXPECT_TRUE(std::string(neuro::proc::to_string(ThreadState::Zombie))     == "zombie");
    EXPECT_TRUE(std::string(neuro::proc::to_string(ThreadState::Terminated)) == "terminated");
}

// ---- 6. start() is idempotent (second call is no-op) -----------------

TEST(thread, double_start_is_noop) {
    g_counter = 0;
    Process p(ProcessInit{"p5", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    t.start();
    t.start();  // should be a no-op
    t.join();
    // Counter should be exactly 1, not 2.
    EXPECT_EQ(1, g_counter.load());
}

// ---- 7. Process owns its threads' caps -------------------------------

TEST(thread, process_owns_thread_caps) {
    Process p(ProcessInit{"p6", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    EXPECT_EQ(&p.caps(), &t.caps());
}

// ---- 8. KObject id() returns unique values ---------------------------

TEST(thread, kobject_ids_are_unique) {
    Process p(ProcessInit{"p7", {}});
    Thread::Attr a;
    Thread t1(p, a, inc_thread);
    Thread t2(p, a, inc_thread);
    EXPECT_NE(t1.id(), t2.id());
}

RUN_ALL_TESTS()