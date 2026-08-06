# justfile — NeuroVerse OS task runner
# Requires: https://github.com/casey/just

# Default recipes come from README §5.
default: build

# ---- Configuration & Build ----

# Generate build files via meson.
configure:
    meson setup build

# Incremental build.
build:
    meson compile -C build

# Clean the build directory.
clean:
    rm -rf build

# ---- Quality ----

# Run unit + integration tests.
test:
    meson test -C build

# Run clang-tidy against sources.
lint:
    clang-tidy --quiet src/**/*.cpp include/**/*.hpp

# Run clang-format in check mode (CI).
format-check:
    clang-format --dry-run --Werror src/**/*.{cpp,hpp} include/**/*.{hpp,h}

# Run clang-format in-place.
format:
    clang-format -i src/**/*.{cpp,hpp} include/**/*.{hpp,h}

# Static analysis via scan-build.
analyze:
    scan-build meson compile -C build

# Run the demo executable built by the Makefile (host build path).
run-demo:
    ./neuro_scratch

# ---- Tools ----

# Run integration tests with ctest verbosely.
test-verbose:
    meson test -C build --print-errorlogs --verbose

# Coverage report.
coverage:
    meson configure -Db_coverage=true build
    meson compile -C build
    meson test -C build
    gcovr --print-summary --html-details coverage.html

# ---- Artifacts (placeholder; real impl in Phase 1+) ----

# Produce a bootable ISO.
iso:
    ./tools/build/iso.sh

# Run the ISO in QEMU.
qemu:
    qemu-system-x86_64 -cdrom build/neuro.iso -m 4096 -smp 4

# Run verification (NeuroProof).
verify:
    ./tools/verify/run.sh

# ---- Convenience ----

# Show this help.
help:
    just --list
