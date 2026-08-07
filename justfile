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

# Coverage report (LLVM on clang, gcov on GCC).
# Produces coverage.html plus a machine-readable XML summary.
coverage:
    #!/usr/bin/env bash
    set -euo pipefail
    rm -rf build-coverage coverage.html coverage.xml
    meson setup build-coverage -Db_coverage=true -Dbuildtype=debug
    meson compile -C build-coverage
    meson test -C build-coverage --print-errorlogs
    gcovr --root . --filter 'include/neuro|src' \
        --exclude 'tests' --print-summary \
        --html-details coverage.html --xml coverage.xml

# ---- Benchmarks & fuzzing ----

# Run every executable under build/benchmarks. If hyperfine is present,
# capture warm-up + repeated timing; otherwise fall back to /usr/bin/time.
bench:
    #!/usr/bin/env bash
    set -euo pipefail
    meson setup build-bench --buildtype=release 2>/dev/null || \
        meson setup --reconfigure build-bench --buildtype=release
    meson compile -C build-bench
    bins="$(find build-bench -type f -perm -111 -path '*/benchmarks/*' | sort)"
    if [[ -z "$bins" ]]; then
        echo 'No benchmark binaries found under build-bench/benchmarks.'
        echo 'Add benchmark sources to benchmarks/ and register them in meson.build.'
        exit 0
    fi
    while IFS= read -r bin; do
        if command -v hyperfine >/dev/null 2>&1; then
            hyperfine --warmup 2 --runs 10 "$bin"
        else
            /usr/bin/time -p "$bin"
        fi
    done <<< "$bins"

# Build and run libFuzzer targets from tests/fuzz for N seconds each.
# Usage: just fuzz N=10s
fuzz N="10s":
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p build-fuzz/corpus build-fuzz/artifacts
    sources="$(find tests/fuzz -maxdepth 1 -name '*_fuzz.cpp' -print 2>/dev/null | sort || true)"
    if [[ -z "$sources" ]]; then
        echo 'No fuzz targets found (expected tests/fuzz/*_fuzz.cpp).'
        exit 0
    fi
    seconds='{{N}}'
    seconds="${seconds%s}"
    if ! [[ "$seconds" =~ ^[0-9]+$ ]]; then
        echo 'N must be an integer number of seconds, for example N=10s' >&2
        exit 2
    fi
    while IFS= read -r source; do
        name="$(basename "$source" _fuzz.cpp)"
        clang++ -std=c++23 -O1 -g -Iinclude -I. \
            -fsanitize=fuzzer,address,undefined \
            "$source" -o "build-fuzz/$name"
        mkdir -p "build-fuzz/corpus/$name" "build-fuzz/artifacts/$name"
        "build-fuzz/$name" "build-fuzz/corpus/$name" \
            -max_total_time="$seconds" \
            -artifact_prefix="build-fuzz/artifacts/$name/"
    done <<< "$sources"

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
