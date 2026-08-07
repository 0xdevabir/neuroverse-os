// neuro/core/version.hpp
//
// NeuroVerse OS project version constants.
//
// Single source of truth for the host scaffold's release version.
// Mirrors the value in `meson.build` — please keep them in sync.
// Pkg-config metadata (Phase P1.3) and the CI release gate
// (Phase P4.2) verify all three surfaces agree.
//
// Versioning follows SemVer 2.0.0 (https://semver.org/).

#pragma once

#include <cstdint>

namespace neuro::core {

// Major version — bumped on incompatible API changes.
inline constexpr std::uint32_t version_major = 0;

// Minor version — bumped on backward-compatible features.
inline constexpr std::uint32_t version_minor = 1;

// Patch version — bumped on backward-compatible fixes.
inline constexpr std::uint32_t version_patch = 0;

// String literal in the canonical "<major>.<minor>.<patch>" form.
inline constexpr const char* version_string = "0.1.0";

// Packed integer form: (major << 24) | (minor << 16) | (patch << 8).
// Convenient for `<`, `>=`, etc. against other NeuroVerse builds.
inline constexpr std::uint32_t version_packed =
    (version_major << 24) | (version_minor << 16) | (version_patch << 8);

}  // namespace neuro::core

// ---- C-macro mirrors ----------------------------------------------------
//
// These macros allow legacy code or generated build files to read the
// version without including the full C++ namespace. They expand to
// integer literals usable in `#if` preprocessor expressions.

#define NEURO_VERSION_MAJOR 0
#define NEURO_VERSION_MINOR 1
#define NEURO_VERSION_PATCH 0
#define NEURO_VERSION_STRING "0.1.0"
#define NEURO_VERSION_PACKED ((0U << 24) | (1U << 16) | (0U << 8))
