// tests/integration/proof_ipc.cpp
//
// Z7.12 — proof contracts gate IPC message construction.
//
// Composes NeuroProof with NeuroIPC. We model "construction of an
// ipc::Message" as a contract-bearing operation:
//
//   * Messages with payload > 64 KiB are rejected (the kernel
//     inlines small args; larger blobs must go through a shared
//     region carved by NeuroMem).
//
//   * The capability handle inside a CapRef must be non-zero
//     (a zero handle is reserved for "no capability").
//
//   * The tag namespace is one of a small enumerated set.
//
// We use `with_strict_precondition` to enforce these at runtime,
// then drive the wrappers with the existing ipc::Message envelope
// and observe the resulting state via ContractStats and the
// Message itself.
//
// The same conditions could in principle be discharged by an
// SMT-backed verifier when NeuroProof lands in Phase 1 — this
// integration test documents the *contract* that the verifier
// will be expected to prove.

#include "tests/test_framework.hpp"

#include "neuro/ipc/message.hpp"
#include "neuro/proof/contract.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

using neuro::ipc::CapRef;
using neuro::ipc::Message;
using neuro::ipc::Tag;
using neuro::proof::ContractError;
using neuro::proof::ContractStats;
using neuro::proof::reset_contract_stats;
using neuro::proof::with_strict_precondition;

namespace {

// Permitted namespaces for our proof integration. In a real kernel
// these would be enumerated by a `Namespace.h` header.
constexpr std::uint16_t kNsCore   = 0x0001;
constexpr std::uint16_t kNsProc   = 0x0002;
constexpr std::uint16_t kNsFs     = 0x0003;
constexpr std::uint16_t kNsNet    = 0x0004;
constexpr std::uint16_t kNsBoot   = 0x0010;

// Payload size cap: matches the inline-bytes budget declared in
// README §4.1.
constexpr std::size_t kMaxInlineBytes = 64 * 1024;

// Pre-condition: namespace is one of the known set.
bool tag_namespace_ok(const Tag& t) noexcept {
    switch (t.ns) {
        case kNsCore:
        case kNsProc:
        case kNsFs:
        case kNsNet:
        case kNsBoot:
            return true;
        default:
            return false;
    }
}

// Pre-condition: cap handle is non-zero.
bool cap_handle_ok(const CapRef& c) noexcept {
    return c.handle != 0;
}

// ---- Contract-bearing factories ----------------------------------------
//
// Each factory wraps the plain ipc::Message ctor with a runtime
// pre-condition check. The expected behaviour: success returns a
// fully-formed Message; failure throws ContractError.

Message make_typed_bytes(Tag t, std::vector<std::byte> payload) {
    return with_strict_precondition(
        [](Tag t_, std::vector<std::byte> p) {
            return Message(t_, std::move(p));
        },
        [](const Tag& t_, const std::vector<std::byte>&) {
            return tag_namespace_ok(t_);
        },
        "tag.ns in {Core, Proc, Fs, Net, Boot}")(
        t, std::move(payload));
}

Message make_typed_bytes_bounded(Tag t, std::vector<std::byte> payload) {
    return with_strict_precondition(
        [](Tag t_, std::vector<std::byte> p) {
            return Message(t_, std::move(p));
        },
        [](const Tag& t_, const std::vector<std::byte>& p) {
            return tag_namespace_ok(t_) && p.size() <= kMaxInlineBytes;
        },
        "tag.ns known AND payload.size() <= 64KiB")(
        t, std::move(payload));
}

Message make_cap_message(Tag t, CapRef c) {
    return with_strict_precondition(
        [](Tag t_, CapRef c_) { return Message(t_, c_); },
        [](const Tag& t_, const CapRef& c_) {
            return tag_namespace_ok(t_) && cap_handle_ok(c_);
        },
        "tag.ns known AND cap.handle != 0")(t, c);
}

}  // namespace

// ---- 1. Allowed namespaces --------------------------------------------

TEST(proof_ipc, known_namespace_passes_precondition) {
    reset_contract_stats();

    Message m = make_typed_bytes(Tag{kNsProc, 7}, {});
    EXPECT_EQ(kNsProc, m.tag.ns);
    EXPECT_EQ(7u, m.tag.op);
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(proof_ipc, unknown_namespace_throws_contract_error) {
    reset_contract_stats();

    bool threw = false;
    try {
        (void)make_typed_bytes(Tag{0xBEEF, 1}, {});
    } catch (const ContractError&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    EXPECT_EQ(1u, ContractStats::instance().precondition_failures);
}

// ---- 2. Inline-payload budget -----------------------------------------

TEST(proof_ipc, small_payload_passes) {
    reset_contract_stats();

    std::vector<std::byte> data(64, std::byte{0xAB});
    Message m = make_typed_bytes_bounded(Tag{kNsCore, 1}, std::move(data));
    EXPECT_EQ(static_cast<std::size_t>(64), m.payload.size());
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(proof_ipc, payload_at_exact_budget_passes) {
    reset_contract_stats();
    std::vector<std::byte> data(kMaxInlineBytes, std::byte{0x00});
    Message m = make_typed_bytes_bounded(Tag{kNsCore, 2}, std::move(data));
    EXPECT_EQ(kMaxInlineBytes, m.payload.size());
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(proof_ipc, payload_over_budget_throws) {
    reset_contract_stats();
    std::vector<std::byte> data(kMaxInlineBytes + 1, std::byte{0x00});

    bool threw = false;
    try {
        (void)make_typed_bytes_bounded(Tag{kNsCore, 3}, std::move(data));
    } catch (const ContractError&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    EXPECT_EQ(1u, ContractStats::instance().precondition_failures);
}

// ---- 3. Capability handle must be non-zero ----------------------------

TEST(proof_ipc, cap_with_nonzero_handle_passes) {
    reset_contract_stats();
    Message m = make_cap_message(Tag{kNsProc, 42}, CapRef{0xCAFEBABE});
    EXPECT_TRUE(m.carries_cap());
    EXPECT_EQ(static_cast<std::uint64_t>(0xCAFEBABE), m.cap->handle);
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(proof_ipc, cap_with_zero_handle_throws) {
    reset_contract_stats();
    bool threw = false;
    try {
        (void)make_cap_message(Tag{kNsProc, 42}, CapRef{0});
    } catch (const ContractError&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    EXPECT_EQ(1u, ContractStats::instance().precondition_failures);
}

// ---- 4. Combined precondition (tag + size + cap) ----------------------

TEST(proof_ipc, all_preconditions_must_hold) {
    reset_contract_stats();

    // Valid in every dimension.
    Message ok =
        make_cap_message(Tag{kNsFs, 9}, CapRef{0x100});
    EXPECT_TRUE(ok.carries_cap());

    // Failures in each of three dimensions all bubble up:
    auto try_one = [](Tag t, CapRef c) {
        try {
            (void)make_cap_message(t, c);
            return false;
        } catch (const ContractError&) {
            return true;
        }
    };

    EXPECT_TRUE(try_one(Tag{0xDEAD, 9}, CapRef{0x100}));   // bad ns
    EXPECT_TRUE(try_one(Tag{kNsFs, 9}, CapRef{0}));        // bad handle
    EXPECT_TRUE(try_one(Tag{0xDEAD, 9}, CapRef{0}));       // both bad
    EXPECT_EQ(3u, ContractStats::instance().precondition_failures);
}

// ---- 5. Message identity is preserved post-check ----------------------

TEST(proof_ipc, returned_message_round_trips_tag_and_cap) {
    reset_contract_stats();
    Message m = make_cap_message(Tag{kNsNet, 0x42}, CapRef{0xABCD});
    EXPECT_EQ(0x42u, m.tag.op);
    EXPECT_EQ(static_cast<std::uint32_t>((kNsNet << 16) | 0x42),
              m.tag.pack());
    EXPECT_TRUE(m.carries_cap());
    EXPECT_EQ(static_cast<std::uint64_t>(0xABCD), m.cap->handle);
    EXPECT_EQ(0u, m.payload.size());
}

// ---- 6. Empty messages still pass the namespace precondition ---------

TEST(proof_ipc, empty_message_with_known_ns_passes) {
    reset_contract_stats();
    Message m = make_typed_bytes(Tag{kNsCore, 0}, {});
    EXPECT_EQ(static_cast<std::size_t>(0), m.payload.size());
    EXPECT_EQ(0u, ContractStats::instance().precondition_failures);
}

TEST(proof_ipc, empty_message_with_unknown_ns_throws) {
    reset_contract_stats();
    bool threw = false;
    try {
        (void)make_typed_bytes(Tag{0xFFFF, 0}, {});
    } catch (const ContractError&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
    EXPECT_EQ(1u, ContractStats::instance().precondition_failures);
}

RUN_ALL_TESTS()