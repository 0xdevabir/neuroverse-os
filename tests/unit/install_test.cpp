// tests/unit/install_test.cpp
//
// Phase P1.2 — install/uninstall contract checks.
//
// These tests shell out to the Makefile to run `install`,
// `install-dry-run`, and `uninstall` against a temporary staged
// DESTDIR. The tests assert that:
//
//   * `install` lays out headers under include/neuro and the
//     static host library under lib/.
//   * `install-dry-run` mentions every file `install` would create
//     without touching the filesystem.
//   * `uninstall` removes every file `install` placed and leaves
//     the staged prefix empty.
//   * The staged public headers are byte-identical to the source.
//
// Each test creates its own temporary directory so they can be
// run in parallel without stepping on each other.

#include "tests/test_framework.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct Cmd {
    int exit_code;
    std::string stdout_log;
    std::string stderr_log;
};

Cmd run(std::string cmd) {
    Cmd r{};
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) {
        std::fprintf(stderr, "popen failed: %s\n", cmd.c_str());
        std::exit(2);
    }
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), p) != nullptr) {
        r.stdout_log.append(buf.data());
    }
    int rc = ::pclose(p);
    r.exit_code = WEXITSTATUS(rc);
    return r;
}

fs::path make_tmp_dir() {
    char tmpl[] = "/tmp/neuro-install-test-XXXXXX";
    char* d = ::mkdtemp(tmpl);
    if (!d) {
        std::fprintf(stderr, "mkdtemp failed\n");
        std::exit(3);
    }
    return fs::path(d);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(install, install_lays_out_headers_and_library) {
    fs::path dest = make_tmp_dir();
    std::string cmd = "cd \"$(pwd)\" && make install DESTDIR=\"" +
                      dest.string() + "\" PREFIX=/usr/local >/dev/null 2>&1";
    Cmd r = run(cmd);
    EXPECT_EQ(0, r.exit_code);

    fs::path include_root = dest / "usr/local/include/neuro";
    fs::path lib_root     = dest / "usr/local/lib";

    // Spot-check: every namespace that has at least one public header
    // must have a header laid out under the include root.
    for (const char* sub : {"core", "sec", "mem", "ipc", "sched", "proc",
                            "net", "fs", "dev", "ui", "audio", "fabric",
                            "pkg", "jit", "proof", "pulse", "learn",
                            "bridge", "boot"}) {
        fs::path d = include_root / sub;
        EXPECT_TRUE(fs::is_directory(d));
        if (fs::is_directory(d)) {
            EXPECT_TRUE(!fs::is_empty(d));
        }
    }

    // Specific headers we absolutely need.
    EXPECT_TRUE(fs::exists(include_root / "neuro.hpp"));
    EXPECT_TRUE(fs::exists(include_root / "core/version.hpp"));
    EXPECT_TRUE(fs::exists(include_root / "core/result.hpp"));

    // Host library.
    EXPECT_TRUE(fs::exists(lib_root / "libneuro_host.a"));

    // Cleanup.
    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/usr/local >/dev/null 2>&1");
}

TEST(install, installed_headers_are_byte_identical_to_sources) {
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/usr/local >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    // Spot-check three headers via byte compare.
    for (const char* rel : {"neuro.hpp", "core/result.hpp",
                            "core/version.hpp"}) {
        fs::path src = fs::path("include/neuro") / rel;
        fs::path dst = dest / "usr/local/include/neuro" / rel;
        EXPECT_TRUE(fs::exists(src));
        EXPECT_TRUE(fs::exists(dst));
        if (fs::exists(src) && fs::exists(dst)) {
            std::ifstream a(src, std::ios::binary);
            std::ifstream b(dst, std::ios::binary);
            std::ostringstream sa, sb;
            sa << a.rdbuf();
            sb << b.rdbuf();
            EXPECT_EQ(sa.str(), sb.str());
        }
    }

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/usr/local >/dev/null 2>&1");
}

TEST(install, uninstall_removes_every_installed_file) {
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/usr/local >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    // Snapshot file count post-install.
    auto count_files = [](const fs::path& root) {
        std::size_t n = 0;
        if (!fs::exists(root)) return n;
        for (auto& e : fs::recursive_directory_iterator(root)) {
            if (e.is_regular_file()) ++n;
        }
        return n;
    };
    std::size_t before = count_files(dest);

    Cmd u = run("make uninstall DESTDIR=\"" + dest.string() +
                "\" PREFIX=/usr/local >/dev/null 2>&1");
    EXPECT_EQ(0, u.exit_code);

    std::size_t after = count_files(dest);
    EXPECT_TRUE(before > 0);
    EXPECT_EQ(0u, after);
}

TEST(install, install_dry_run_lists_files_without_writing) {
    fs::path dest = make_tmp_dir();
    std::string cmd = "make install-dry-run DESTDIR=\"" + dest.string() +
                      "\" PREFIX=/usr/local 2>&1";
    Cmd r = run(cmd);
    EXPECT_EQ(0, r.exit_code);

    EXPECT_TRUE(contains(r.stdout_log, "libneuro_host.a"));
    EXPECT_TRUE(contains(r.stdout_log, "neuro.hpp"));
    EXPECT_TRUE(contains(r.stdout_log, "core/version.hpp"));

    // Nothing should have been written.
    EXPECT_TRUE(fs::is_empty(dest));
}

TEST(install, install_respects_custom_prefix) {
    fs::path dest = make_tmp_dir();
    fs::path custom = dest / "opt/neuro";
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/opt/neuro >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    EXPECT_TRUE(fs::exists(custom / "include/neuro/neuro.hpp"));
    EXPECT_TRUE(fs::exists(custom / "lib/libneuro_host.a"));

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/opt/neuro >/dev/null 2>&1");
}

RUN_ALL_TESTS()
