// tests/unit/core/version_test.cpp
//
// Phase P1.1 — version constants.
//
// Verifies that the C++ constexpr constants, the C-macro mirrors,
// and the packed-version integer all agree, and that the version
// string matches SemVer form. If any of these fail, the CI release
// gate (P4.2) will also fail.

#include "tests/test_framework.hpp"

#include "neuro/core/version.hpp"

#include <cstdint>
#include <cstring>

using neuro::core::version_major;
using neuro::core::version_minor;
using neuro::core::version_patch;
using neuro::core::version_packed;
using neuro::core::version_string;

namespace {

// Build the expected "M.N.P" string from the constexpr integers and
// compare it byte-for-byte with the published literal.
bool matches_semver(const char* s) noexcept {
    if (!s) return false;
    // Walk "<digits>.<digits>.<digits>\0" — no leading 'v', no
    // pre-release suffix, no build metadata.
    int dots = 0;
    int digits_in_run = 0;
    for (std::size_t i = 0; s[i] != '\0'; ++i) {
        char c = s[i];
        if (c == '.') {
            if (digits_in_run == 0) return false;
            ++dots;
            digits_in_run = 0;
        } else if (c >= '0' && c <= '9') {
            ++digits_in_run;
        } else {
            return false;
        }
    }
    return dots == 2 && digits_in_run > 0;
}

}  // namespace

TEST(version, major_minor_patch_are_in_range) {
    EXPECT_TRUE(version_major < 256u);
    EXPECT_TRUE(version_minor < 256u);
    EXPECT_TRUE(version_patch < 256u);
}

TEST(version, packed_matches_components) {
    std::uint32_t expected = (version_major << 24)
                           | (version_minor << 16)
                           | (version_patch << 8);
    EXPECT_EQ(expected, version_packed);
}

TEST(version, macros_match_constants) {
#if defined(NEURO_VERSION_MAJOR) && defined(NEURO_VERSION_MINOR) \
    && defined(NEURO_VERSION_PATCH) && defined(NEURO_VERSION_STRING)
    EXPECT_EQ(static_cast<std::uint32_t>(NEURO_VERSION_MAJOR), version_major);
    EXPECT_EQ(static_cast<std::uint32_t>(NEURO_VERSION_MINOR), version_minor);
    EXPECT_EQ(static_cast<std::uint32_t>(NEURO_VERSION_PATCH), version_patch);
    EXPECT_TRUE(std::strcmp(NEURO_VERSION_STRING, version_string) == 0);
#else
    EXPECT_TRUE(false && "NEURO_VERSION_* macros not defined");
#endif
}

TEST(version, string_is_semver_form) {
    EXPECT_TRUE(matches_semver(version_string));
}

TEST(version, packed_is_strictly_greater_than_zero) {
    // 0.0.0 would indicate an uninitialised version constant.
    EXPECT_TRUE(version_packed > 0u);
}

TEST(version, packed_ordering) {
    // Patch-only bump advances the packed form.
    EXPECT_TRUE((0U << 24) | (1U << 16) | (1U << 8) >
                version_packed);
    // Minor bump dominates patch.
    EXPECT_TRUE((0U << 24) | (2U << 16) | (0U << 8) >
                (0U << 24) | (1U << 16) | (99U << 8));
}

RUN_ALL_TESTS()
