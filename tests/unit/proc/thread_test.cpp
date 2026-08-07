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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// ---- 9. default Attr values -----------------------------------------

TEST(thread, default_attr_values) {
    Process p(ProcessInit{"p8", {}});
    Thread::Attr a;  // all defaults
    Thread t(p, a, inc_thread);
    EXPECT_TRUE(t.name().empty());
    EXPECT_EQ(0u,           t.affinity());
    EXPECT_EQ(100,          t.priority());
    EXPECT_EQ(&p,           &t.owner());
}

// ---- 10. state machine: Running → Zombie ----------------------------

TEST(thread, state_zombie_after_entry_returns) {
    Process p(ProcessInit{"p9", {}});
    Thread::Attr a;
    a.name = "quick";
    std::atomic<bool> entered{false};
    Thread t(p, a, [&entered](Thread& /*th*/) {
        entered.store(true);
        // exit immediately so the run_loop marks Zombie
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    t.join();
    EXPECT_EQ(ThreadState::Terminated, t.state());
}

// ---- 11. destruction joins a running thread --------------------------

TEST(thread, destructor_joins_thread) {
    Process p(ProcessInit{"p10", {}});
    std::atomic<bool> ran{false};
    {
        Thread::Attr a;
        Thread t(p, a, [&ran](Thread&) {
            ran.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
        t.start();
        // Do NOT explicitly join — destructor must do it.
        (void)t.state();
    }
    // If we got here without hanging, the destructor joined.
    EXPECT_TRUE(ran.load());
}

// ---- 12. join after the thread is already done is safe --------------

TEST(thread, double_join_is_safe) {
    Process p(ProcessInit{"p11", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    t.start();
    t.join();
    // std::thread::join() is idempotent on an unjoinable thread; this
    // call must not crash or deadlock.
    t.join();
    EXPECT_EQ(ThreadState::Terminated, t.state());
}

// ---- 13. many threads in one process run in parallel ----------------

TEST(thread, many_threads_in_one_process) {
    constexpr int N = 16;
    Process p(ProcessInit{"p12", {}});
    std::vector<std::unique_ptr<Thread>> ts;
    std::atomic<int> counter{0};
    ts.reserve(N);
    for (int i = 0; i < N; ++i) {
        Thread::Attr a;
        a.name = "worker_" + std::to_string(i);
        ts.push_back(std::make_unique<Thread>(
            p, a, [&counter](Thread&) {
                counter.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }));
        ts.back()->start();
    }
    for (auto& up : ts) up->join();
    EXPECT_EQ(N, counter.load());
    for (auto& up : ts) {
        EXPECT_EQ(ThreadState::Terminated, up->state());
    }
}

// ---- 14. wake() semantics ------------------------------------------

TEST(thread, wake_on_running_thread_is_noop) {
    // wake() only transitions Waiting -> Ready; calling it on a Running
    // thread must not crash and must not change the state.
    Process p(ProcessInit{"p13", {}});
    Thread::Attr a;
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(ThreadState::Running, t.state());
    t.wake();    // Running -> Running, no-op
    EXPECT_EQ(ThreadState::Running, t.state());
    stop.store(true);
    t.join();
}

TEST(thread, wake_is_idempotent) {
    // Many wake() calls on the same thread must not deadlock or crash.
    Process p(ProcessInit{"p14", {}});
    Thread::Attr a;
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    for (int i = 0; i < 100; ++i) t.wake();
    EXPECT_EQ(ThreadState::Running, t.state());
    stop.store(true);
    t.join();
}

TEST(thread, wake_on_terminated_thread_is_safe) {
    // Calling wake() on a thread that has already exited must not crash
    // or change the state. The underlying cv_.notify_one() on a not-yet-
    // destroyed cv is well-defined.
    Process p(ProcessInit{"p15", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    t.start();
    t.join();
    t.wake();
    EXPECT_EQ(ThreadState::Terminated, t.state());
}

TEST(thread, wake_on_unstarted_thread_is_safe) {
    // wake() must not crash even before start(). The state is Ready
    // and wake() is a no-op.
    Process p(ProcessInit{"p16", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    EXPECT_EQ(ThreadState::Ready, t.state());
    t.wake();
    EXPECT_EQ(ThreadState::Ready, t.state());
    t.start();
    t.join();
}

// ---- 15. join_for() returns true when entry finishes in time -------

TEST(thread, join_for_returns_true_for_fast_entry) {
    // join_for returns true once done_ is set, but state remains Zombie
    // until a subsequent join() bumps it to Terminated. Document the
    // asymmetry.
    Process p(ProcessInit{"p17", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    t.start();
    bool ok = t.join_for(std::chrono::milliseconds{500});
    EXPECT_TRUE(ok);
    EXPECT_EQ(ThreadState::Zombie, t.state());
    // Calling join() now transitions Zombie -> Terminated.
    t.join();
    EXPECT_EQ(ThreadState::Terminated, t.state());
}

TEST(thread, join_for_returns_false_for_blocked_entry) {
    // Entry blocks on an external flag; join_for(short) must time out
    // and return false without joining.
    Process p(ProcessInit{"p18", {}});
    Thread::Attr a;
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    bool ok = t.join_for(std::chrono::milliseconds{10});
    EXPECT_FALSE(ok);

    // The thread is still running — clean up.
    stop.store(true);
    t.join();
    EXPECT_EQ(ThreadState::Terminated, t.state());
}

TEST(thread, join_for_before_start_returns_true) {
    // If start() was never called, the OS thread is not joinable and
    // join_for must return true (no thread to wait for).
    Process p(ProcessInit{"p19", {}});
    Thread::Attr a;
    Thread t(p, a, inc_thread);
    bool ok = t.join_for(std::chrono::milliseconds{10});
    EXPECT_TRUE(ok);
}

// ---- 16. destructor joins detached thread --------------------------

TEST(thread, destructor_joins_long_running_entry) {
    // Construct a thread whose entry runs for longer than the scope;
    // the destructor must wait for the entry to finish before returning.
    // An external thread flips `may_exit` after 25 ms to unblock the entry.
    Process p(ProcessInit{"p20", {}});
    std::atomic<bool> ran{false};
    std::atomic<bool> may_exit{false};
    std::thread flipper([&may_exit] {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        may_exit.store(true);
    });
    {
        Thread::Attr a;
        Thread t(p, a, [&ran, &may_exit](Thread&) {
            ran.store(true);
            while (!may_exit.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        t.start();
        while (!ran.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // t goes out of scope without explicit join(); the flipper
        // will release the entry so the destructor can return.
    }
    flipper.join();
    EXPECT_TRUE(ran.load());
    EXPECT_TRUE(may_exit.load());
}

TEST(thread, destructor_on_unstarted_thread_is_noop) {
    // A Thread that was never started must have a destructor that does
    // nothing observable.
    Process p(ProcessInit{"p21", {}});
    {
        Thread::Attr a;
        Thread t(p, a, inc_thread);
        (void)t.state();
        // t dies here without start() or join().
    }
    // If we got here, destructor was a no-op.
    EXPECT_TRUE(true);
}

// ---- 17. wake() idempotence across many calls from one thread -----

TEST(thread, wake_many_calls_from_one_thread) {
    // A loop of 1000 wake() calls from a single outside thread must
    // not deadlock or corrupt state.
    Process p(ProcessInit{"p22", {}});
    Thread::Attr a;
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    for (int i = 0; i < 1000; ++i) t.wake();
    EXPECT_EQ(ThreadState::Running, t.state());
    stop.store(true);
    t.join();
}

TEST(thread, wake_from_many_threads_concurrently) {
    // Multiple outside threads each calling wake() in a tight loop
    // must not corrupt state.
    Process p(ProcessInit{"p23", {}});
    Thread::Attr a;
    std::atomic<bool> entered{false};
    std::atomic<bool> stop{false};
    Thread t(p, a, [&entered, &stop](Thread& th) {
        entered.store(true);
        while (!stop.load() && th.state() != ThreadState::Terminated) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    t.start();
    while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    constexpr int kWorkers = 4;
    constexpr int kIters = 250;
    std::vector<std::thread> ws;
    for (int i = 0; i < kWorkers; ++i) {
        ws.emplace_back([&t] {
            for (int j = 0; j < kIters; ++j) t.wake();
        });
    }
    for (auto& w : ws) w.join();

    EXPECT_EQ(ThreadState::Running, t.state());
    stop.store(true);
    t.join();
}

RUN_ALL_TESTS()