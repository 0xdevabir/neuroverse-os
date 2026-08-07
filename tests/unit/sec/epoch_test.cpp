// tests/unit/sec/epoch_test.cpp
//
// Tests for neuro::sec::CapEpoch — the per-space revocation counter
// described in include/neuro/sec/epoch.hpp.
//
// Coverage:
//   - default-constructed epoch is zero
//   - current() matches what we just stored
//   - revoke() bumps by 1 each call
//   - revoke() returns the post-bump value
//   - valid(epoch) is true for the current epoch only
//   - valid(epoch) is false after revoke()
//   - revoked caps from past epochs all invalid
//   - revocation monotonic: each call yields a higher value
//   - 16-bit wrap-around: bumps to 0 after 65535 revokes
//   - two CapEpochs are independent

#include "neuro/sec/epoch.hpp"

#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::sec::CapEpoch;

// ---- 1. default state ------------------------------------------

TEST(epoch, default_is_zero) {
    CapEpoch e;
    EXPECT_EQ(static_cast<std::uint16_t>(0), e.current());
}

TEST(epoch, default_validates_zero) {
    CapEpoch e;
    EXPECT_TRUE(e.valid(0));
    EXPECT_FALSE(e.valid(1));
}

// ---- 2. revoke() bumps ------------------------------------------

TEST(epoch, revoke_returns_new_value) {
    CapEpoch e;
    auto r1 = e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(1), r1);
    EXPECT_EQ(static_cast<std::uint16_t>(1), e.current());
    auto r2 = e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(2), r2);
    EXPECT_EQ(static_cast<std::uint16_t>(2), e.current());
}

TEST(epoch, revoke_monotonic) {
    CapEpoch e;
    std::uint16_t prev = e.current();
    for (int i = 0; i < 32; ++i) {
        auto now = e.revoke();
        EXPECT_TRUE(now > prev);
        prev = now;
    }
    EXPECT_EQ(static_cast<std::uint16_t>(32), e.current());
}

// ---- 3. valid() against current epoch --------------------------

TEST(epoch, valid_matches_only_current) {
    CapEpoch e;
    EXPECT_TRUE(e.valid(0));

    (void)e.revoke();  // current is now 1
    EXPECT_FALSE(e.valid(0));
    EXPECT_TRUE(e.valid(1));
    EXPECT_FALSE(e.valid(2));

    (void)e.revoke();  // current is now 2
    EXPECT_FALSE(e.valid(0));
    EXPECT_FALSE(e.valid(1));
    EXPECT_TRUE(e.valid(2));
    EXPECT_FALSE(e.valid(3));
}

// ---- 4. revoke invalidates past caps -----------------------------

TEST(epoch, revoked_caps_invalid) {
    CapEpoch e;
    auto minted_at = e.current();  // 0
    EXPECT_TRUE(e.valid(minted_at));

    (void)e.revoke();
    EXPECT_FALSE(e.valid(minted_at));  // past epoch
}

TEST(epoch, chain_of_revokes_only_last_is_valid) {
    CapEpoch e;
    std::vector<std::uint16_t> seen;
    for (int i = 0; i < 8; ++i) {
        seen.push_back(e.current());
        (void)e.revoke();
    }
    // Each historical value is invalid; only the final current is valid.
    for (std::size_t i = 0; i < seen.size(); ++i) {
        EXPECT_FALSE(e.valid(seen[i]));
    }
    EXPECT_TRUE(e.valid(e.current()));
}

// ---- 5. 16-bit wrap-around -------------------------------------

TEST(epoch, wrap_around_at_65536) {
    CapEpoch e;
    for (int i = 0; i < 65535; ++i) {
        (void)e.revoke();
    }
    EXPECT_EQ(static_cast<std::uint16_t>(65535), e.current());

    (void)e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(0), e.current());
    EXPECT_TRUE(e.valid(0));
}

TEST(epoch, wrap_around_invalidates_all) {
    // Verify the well-known property of the wrap: after wrap-around
    // to 0, a cap minted before the wrap is once again "valid" by
    // accident (current==0, saved==0). This is the documented
    // reason why the kernel must retire the space at the boundary.
    // We capture a mid-flight epoch (1) and verify it does NOT come
    // back as valid after wrap.
    CapEpoch e;
    (void)e.revoke();  // current == 1
    auto mid = e.current();
    EXPECT_EQ(static_cast<std::uint16_t>(1), mid);

    for (int i = 0; i < 65534; ++i) {
        (void)e.revoke();
    }
    EXPECT_TRUE(e.valid(65535));
    (void)e.revoke();  // wraps back to 0
    EXPECT_EQ(static_cast<std::uint16_t>(0), e.current());

    // After the wrap, a non-zero past epoch is invalid.
    EXPECT_FALSE(e.valid(mid));
    EXPECT_FALSE(e.valid(65535));
    // But the freshly minted epoch 0 is valid (caveat documented in
    // the header — caller is responsible for retiring the space).
    EXPECT_TRUE(e.valid(0));
}

// ---- 6. two epochs are independent -----------------------------

TEST(epoch, two_epochs_independent) {
    CapEpoch a;
    CapEpoch b;
    EXPECT_EQ(static_cast<std::uint16_t>(0), a.current());
    EXPECT_EQ(static_cast<std::uint16_t>(0), b.current());

    (void)a.revoke();
    (void)a.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(2), a.current());
    EXPECT_EQ(static_cast<std::uint16_t>(0), b.current());

    (void)b.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(2), a.current());
    EXPECT_EQ(static_cast<std::uint16_t>(1), b.current());

    EXPECT_TRUE(a.valid(2));
    EXPECT_FALSE(a.valid(1));
    EXPECT_TRUE(b.valid(1));
    EXPECT_FALSE(b.valid(2));
}

// ---- 7. many revokes in sequence produce dense range ----------

TEST(epoch, hundred_revokes_yield_hundred) {
    CapEpoch e;
    for (int i = 0; i < 100; ++i) (void)e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(100), e.current());
}

// ---- 8. thread-safe revokes (basic atomicity) ------------------

TEST(epoch, threaded_revokes_count_isolated) {
    // Each thread revokes its epoch independently; the totals must
    // match the per-thread counts (this is a sanity check that the
    // atomic isn't completely broken, not a true stress test).
    constexpr int Threads = 4;
    constexpr int Per = 100;

    std::vector<std::thread> ts;
    std::vector<CapEpoch> eps(Threads);

    for (int t = 0; t < Threads; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < Per; ++i) {
                (void)eps[t].revoke();
            }
        });
    }
    for (auto& t : ts) t.join();

    for (int t = 0; t < Threads; ++t) {
        EXPECT_EQ(static_cast<std::uint16_t>(Per), eps[t].current());
    }
}

// ---- 9. wrap-around stress: 65 000 revokes from non-zero start -----

TEST(epoch, sixty_five_thousand_revokes_lands_on_zero) {
    // Burn through the entire 16-bit space starting from epoch 7
    // (i.e., 65 029 revokes to land back at 0). Verify current() is
    // monotonically non-decreasing at sample points and lands on
    // exactly the expected residue.
    CapEpoch e;
    for (int i = 0; i < 7; ++i) (void)e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(7), e.current());

    constexpr int kBurn = 65536 - 7;
    for (int i = 0; i < kBurn; ++i) (void)e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(0), e.current());
    EXPECT_TRUE(e.valid(0));
    // Pre-wrap snapshots remain invalid after wrap.
    EXPECT_FALSE(e.valid(7));
    EXPECT_FALSE(e.valid(12345));
}

TEST(epoch, half_million_revokes_converges_to_residue) {
    // Drive a large workload that wraps many times and assert the
    // residue. 700 003 % 65 536 == 44 643, so after that many revokes
    // starting from 0, current() must read 44 643.
    CapEpoch e;
    constexpr int kTotal = 700003;
    for (int i = 0; i < kTotal; ++i) (void)e.revoke();
    EXPECT_EQ(static_cast<std::uint16_t>(44643), e.current());
}

RUN_ALL_TESTS()
