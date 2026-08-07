// tests/unit/fs/overlayfs_test.cpp
//
// Direct unit tests for OverlayVNode and OverlayFS. The integration
// test only checks copy-up behavior end-to-end; here we isolate the
// contract: upper-preferred reads, write/truncate require upper, stat
// preference, copy-up copy semantics, and the various open flag paths.

#include "neuro/fs/memfs.hpp"
#include "neuro/fs/overlayfs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../test_framework.hpp"

using neuro::core::ErrorKind;
using neuro::fs::FileType;
using neuro::fs::MemFS;
using neuro::fs::OpenFlags;
using neuro::fs::OverlayFS;
using neuro::fs::OverlayVNode;

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

std::string text(const std::vector<std::byte>& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

}  // namespace

// ---- OverlayVNode read contract --------------------------------------

TEST(overlayfs, vnode_read_prefers_upper) {
    auto upper = std::make_unique<MemFS>();
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(upper->write_all("/file", bytes("UPPER")).has_value());
    EXPECT_TRUE(lower->write_all("/file", bytes("lower")).has_value());

    OverlayVNode node(1, *upper->lookup("/file"),
                          *lower->lookup("/file"));
    std::array<std::byte, 6> buf{};
    auto r = node.read(0, std::span<std::byte>(buf.data(), buf.size()));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), *r);
    EXPECT_EQ(static_cast<std::uint8_t>('U'), static_cast<std::uint8_t>(buf[0]));
}

TEST(overlayfs, vnode_read_falls_through_to_lower) {
    // Lower has data; upper has nothing for this path.
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(lower->write_all("/only-lower", bytes("hello")).has_value());

    OverlayVNode node(2, /*upper=*/nullptr, *lower->lookup("/only-lower"));

    std::array<std::byte, 8> buf{};
    auto r = node.read(0, std::span<std::byte>(buf.data(), buf.size()));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::size_t>(5), *r);
    EXPECT_EQ(static_cast<std::uint8_t>('h'), static_cast<std::uint8_t>(buf[0]));
}

TEST(overlayfs, vnode_read_with_no_layers_errors) {
    OverlayVNode node(3, /*upper=*/nullptr, /*lower=*/nullptr);
    std::array<std::byte, 4> buf{};
    auto r = node.read(0, std::span<std::byte>(buf.data(), buf.size()));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(ErrorKind::NotFound, r.error().kind);
}

// ---- OverlayVNode write contract -------------------------------------

TEST(overlayfs, vnode_write_requires_upper) {
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(lower->write_all("/file", bytes("lower")).has_value());

    OverlayVNode node(4, /*upper=*/nullptr, *lower->lookup("/file"));
    auto w = node.write(0, bytes("anything"));
    EXPECT_FALSE(w.has_value());
    EXPECT_EQ(ErrorKind::NotPermitted, w.error().kind);
}

TEST(overlayfs, vnode_write_routes_to_upper) {
    auto upper = std::make_unique<MemFS>();
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(lower->write_all("/file", bytes("lower")).has_value());
    // Pre-populate upper with a sentinel that we'll then overwrite.
    EXPECT_TRUE(upper->write_all("/file", bytes("UPPER")).has_value());

    OverlayVNode bound(5, *upper->lookup("/file"),
                            *lower->lookup("/file"));
    // Truncate to the new length first, then write — read does not
    // include a trailing byte beyond the write size.
    EXPECT_TRUE(bound.truncate(3).has_value());
    auto w = bound.write(0, bytes("REP"));
    EXPECT_TRUE(w.has_value());

    auto got = upper->read_all("/file");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(std::string("REP"), text(*got));
}

// ---- OverlayVNode truncate + stat contract --------------------------

TEST(overlayfs, vnode_truncate_requires_upper) {
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(lower->write_all("/file", bytes("payload")).has_value());

    OverlayVNode node(6, /*upper=*/nullptr, *lower->lookup("/file"));
    auto t = node.truncate(2);
    EXPECT_FALSE(t.has_value());
    EXPECT_EQ(ErrorKind::NotPermitted, t.error().kind);
}

TEST(overlayfs, vnode_stat_prefers_upper) {
    auto upper = std::make_unique<MemFS>();
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(upper->write_all("/file", bytes("U")).has_value());  // 1 byte
    EXPECT_TRUE(lower->write_all("/file", bytes("LLLLLL")).has_value());  // 6 bytes

    OverlayVNode node(7, *upper->lookup("/file"),
                          *lower->lookup("/file"));
    auto s = node.stat();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(1), s->size);
    EXPECT_EQ(FileType::Regular, s->type);
}

TEST(overlayfs, vnode_stat_falls_back_to_lower) {
    auto lower = std::make_unique<MemFS>();
    EXPECT_TRUE(lower->write_all("/file", bytes("hello")).has_value());

    OverlayVNode node(8, /*upper=*/nullptr, *lower->lookup("/file"));
    auto s = node.stat();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(5), s->size);
}

TEST(overlayfs, vnode_stat_with_no_layers_errors) {
    OverlayVNode node(9, /*upper=*/nullptr, /*lower=*/nullptr);
    auto s = node.stat();
    EXPECT_FALSE(s.has_value());
    EXPECT_EQ(ErrorKind::NotFound, s.error().kind);
}

// ---- OverlayFS read-side dispatch -----------------------------------

TEST(overlayfs, fs_lookup_prefers_upper) {
    auto upper_owned = std::make_unique<MemFS>();
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(upper_owned->write_all("/file", bytes("U")).has_value());
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("LL")).has_value());

    OverlayFS overlay(*lower_owned);
    overlay.set_upper(std::move(upper_owned));

    auto v = overlay.lookup("/file");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(1), (*v)->stat()->size);
}

TEST(overlayfs, fs_lookup_falls_through_to_lower) {
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("only")).has_value());

    OverlayFS overlay(*lower_owned);
    overlay.set_upper(std::make_unique<MemFS>());

    auto v = overlay.lookup("/file");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(4), (*v)->stat()->size);
}

TEST(overlayfs, fs_lookup_missing_returns_not_found) {
    auto lower_owned = std::make_unique<MemFS>();
    OverlayFS overlay(*lower_owned);
    overlay.set_upper(std::make_unique<MemFS>());

    auto v = overlay.lookup("/missing");
    EXPECT_FALSE(v.has_value());
    EXPECT_EQ(ErrorKind::NotFound, v.error().kind);
}

TEST(overlayfs, fs_read_all_uses_overlay_precedence) {
    auto upper_owned = std::make_unique<MemFS>();
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(upper_owned->write_all("/file", bytes("OVERRIDE")).has_value());
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("original")).has_value());

    OverlayFS overlay(*lower_owned);
    overlay.set_upper(std::move(upper_owned));

    auto got = overlay.read_all("/file");
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(std::string("OVERRIDE"), text(*got));
}

// ---- OverlayFS open / write paths -----------------------------------

TEST(overlayfs, open_read_does_not_copy_up) {
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("original")).has_value());

    OverlayFS overlay(*lower_owned);
    auto upper = std::make_unique<MemFS>();
    MemFS* upper_raw = upper.get();
    overlay.set_upper(std::move(upper));

    auto fh = overlay.open("/file", OpenFlags::Read);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(static_cast<std::size_t>(0), upper_raw->size());
}

TEST(overlayfs, open_with_write_triggers_copy_up) {
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("original")).has_value());

    OverlayFS overlay(*lower_owned);
    auto upper = std::make_unique<MemFS>();
    MemFS* upper_raw = upper.get();
    overlay.set_upper(std::move(upper));

    auto fh = overlay.open("/file", OpenFlags::Write);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), upper_raw->size());

    auto upper_payload = upper_raw->read_all("/file");
    EXPECT_TRUE(upper_payload.has_value());
    EXPECT_EQ(std::string("original"), text(*upper_payload));
}

TEST(overlayfs, open_with_create_lands_in_upper) {
    OverlayFS overlay(*std::make_unique<MemFS>());
    auto upper = std::make_unique<MemFS>();
    MemFS* upper_raw = upper.get();
    overlay.set_upper(std::move(upper));

    auto fh = overlay.open("/fresh", OpenFlags::Create | OpenFlags::Write);
    EXPECT_TRUE(fh.has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), upper_raw->size());

    auto payload = upper_raw->read_all("/fresh");
    EXPECT_TRUE(payload.has_value());
    EXPECT_TRUE(payload->empty());
}

TEST(overlayfs, open_missing_without_create_errors) {
    OverlayFS overlay(*std::make_unique<MemFS>());
    overlay.set_upper(std::make_unique<MemFS>());

    auto fh = overlay.open("/nope", OpenFlags::Read);
    EXPECT_FALSE(fh.has_value());
    EXPECT_EQ(ErrorKind::NotFound, fh.error().kind);
}

TEST(overlayfs, write_all_replaces_upper_payload_preserves_lower) {
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(lower_owned->write_all("/etc/hostname", bytes("node-01"))
                    .has_value());

    OverlayFS overlay(*lower_owned);
    auto upper = std::make_unique<MemFS>();
    MemFS* upper_raw = upper.get();
    overlay.set_upper(std::move(upper));

    // First write triggers copy-up.
    auto w = overlay.write_all("/etc/hostname", bytes("node-99"));
    EXPECT_TRUE(w.has_value());

    // Upper reflects new value.
    auto up = upper_raw->read_all("/etc/hostname");
    EXPECT_TRUE(up.has_value());
    EXPECT_EQ(std::string("node-99"), text(*up));

    // Lower is unchanged.
    auto lw = lower_owned->read_all("/etc/hostname");
    EXPECT_TRUE(lw.has_value());
    EXPECT_EQ(std::string("node-01"), text(*lw));
}

TEST(overlayfs, copy_up_is_idempotent) {
    auto lower_owned = std::make_unique<MemFS>();
    EXPECT_TRUE(lower_owned->write_all("/file", bytes("first")).has_value());

    OverlayFS overlay(*lower_owned);
    auto upper = std::make_unique<MemFS>();
    MemFS* upper_raw = upper.get();
    overlay.set_upper(std::move(upper));

    EXPECT_TRUE(overlay.open("/file", OpenFlags::Write).has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), upper_raw->size());
    auto* first_v = *upper_raw->lookup("/file");
    const auto first_id = first_v->handle().id;

    // A second open-for-write must not allocate a new upper VNode.
    EXPECT_TRUE(overlay.open("/file", OpenFlags::Write).has_value());
    EXPECT_EQ(static_cast<std::size_t>(1), upper_raw->size());
    auto* second_v = *upper_raw->lookup("/file");
    EXPECT_EQ(first_id, second_v->handle().id);
}

RUN_ALL_TESTS()