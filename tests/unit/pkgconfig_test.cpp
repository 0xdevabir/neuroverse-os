// tests/unit/pkgconfig_test.cpp
//
// Phase P1.3 — pkg-config metadata contract checks.
//
// Stages a temporary install and shells out to pkg-config with
// PKG_CONFIG_PATH set to the staged lib/pkgconfig directory. The
// tests assert that:
//
//   * `pkg-config --modversion neuroverse-os` returns the version
//     baked into the .pc file (kept in lockstep with the C++
//     version constants).
//   * `pkg-config --variable=includedir ...` matches the staged
//     include prefix exactly.
//   * `pkg-config --cflags --libs ...` produces a compilable
//     command line. We compile and run a tiny program against the
//     produced flags.
//
// The test is structured the same way as install_test: it owns its
// own temporary directory and is intended for the `test-install`
// target, not `make test` (which would race against itself).

#include "tests/test_framework.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct Cmd {
    int exit_code;
    std::string stdout_log;
};

Cmd run(const std::string& cmd) {
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
    char tmpl[] = "/tmp/neuro-pkgconfig-test-XXXXXX";
    char* d = ::mkdtemp(tmpl);
    if (!d) {
        std::fprintf(stderr, "mkdtemp failed\n");
        std::exit(3);
    }
    return fs::path(d);
}

std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    auto end   = s.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    return s.substr(begin, end - begin + 1);
}

bool pkg_config_available() {
    Cmd probe = run("command -v pkg-config >/dev/null 2>&1; "
                    "echo \"$?\"");
    return trim(probe.stdout_log) == "0";
}

}  // namespace

TEST(pkgconfig, generated_file_has_expanded_prefix_and_version) {
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/opt/neuro >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    fs::path pc = dest / "opt/neuro/lib/pkgconfig/neuroverse-os.pc";
    EXPECT_TRUE(fs::exists(pc));
    std::ifstream in(pc);
    std::ostringstream text;
    text << in.rdbuf();
    std::string contents = text.str();
    EXPECT_TRUE(contents.find("prefix=/opt/neuro") != std::string::npos);
    EXPECT_TRUE(contents.find("Version: 0.1.0") != std::string::npos);
    EXPECT_TRUE(contents.find("-lneuro_host") != std::string::npos);
    EXPECT_TRUE(contents.find("@PREFIX@") == std::string::npos);
    EXPECT_TRUE(contents.find("@VERSION@") == std::string::npos);

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/opt/neuro >/dev/null 2>&1");
}

TEST(pkgconfig, installed_pc_file_reports_correct_version) {
    if (!pkg_config_available()) {
        std::printf("[SKIP ] pkg-config not on PATH\n");
        return;
    }
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/usr/local >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    fs::path pc_dir = dest / "usr/local/lib/pkgconfig";
    EXPECT_TRUE(fs::exists(pc_dir / "neuroverse-os.pc"));

    Cmd v = run("PKG_CONFIG_PATH=\"" + pc_dir.string() + "\" "
                "pkg-config --modversion neuroverse-os");
    EXPECT_EQ(0, v.exit_code);
    EXPECT_EQ(std::string("0.1.0"), trim(v.stdout_log));

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/usr/local >/dev/null 2>&1");
}

TEST(pkgconfig, includedir_variable_matches_installed_prefix) {
    if (!pkg_config_available()) {
        std::printf("[SKIP ] pkg-config not on PATH\n");
        return;
    }
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/opt/neuro >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    fs::path pc_dir = dest / "opt/neuro/lib/pkgconfig";
    Cmd id = run("PKG_CONFIG_PATH=\"" + pc_dir.string() + "\" "
                 "pkg-config --variable=includedir neuroverse-os");
    EXPECT_EQ(0, id.exit_code);
    EXPECT_EQ(std::string("/opt/neuro/include"), trim(id.stdout_log));

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/opt/neuro >/dev/null 2>&1");
}

TEST(pkgconfig, cflags_and_libs_produce_compilable_command) {
    if (!pkg_config_available()) {
        std::printf("[SKIP ] pkg-config not on PATH\n");
        return;
    }
    fs::path dest = make_tmp_dir();
    Cmd r = run("make install DESTDIR=\"" + dest.string() +
                "\" PREFIX=/usr/local >/dev/null 2>&1");
    EXPECT_EQ(0, r.exit_code);

    fs::path pc_dir = dest / "usr/local/lib/pkgconfig";

    // Pull cflags + libs separately and assemble a compile command.
    Cmd cf = run("PKG_CONFIG_PATH=\"" + pc_dir.string() + "\" "
                 "pkg-config --cflags neuroverse-os");
    Cmd lf = run("PKG_CONFIG_PATH=\"" + pc_dir.string() + "\" "
                 "pkg-config --libs neuroverse-os");
    EXPECT_EQ(0, cf.exit_code);
    EXPECT_EQ(0, lf.exit_code);

    // Compile a one-liner that only references the version constant.
    fs::path src = dest / "probe.cpp";
    {
        std::ofstream f(src);
        f << "#include \"neuro/core/version.hpp\"\n"
          << "int main(){return neuro::core::version_major != 0;}\n";
    }
    fs::path bin = dest / "probe.bin";
    std::string cmd = "clang++ -std=c++23 -O2 " + trim(cf.stdout_log) +
                      " " + src.string() + " " + trim(lf.stdout_log) +
                      " -o " + bin.string() + " 2>&1";
    Cmd build = run(cmd);
    EXPECT_EQ(0, build.exit_code);

    Cmd run_cmd = run(bin.string());
    EXPECT_EQ(0, run_cmd.exit_code);

    run("make uninstall DESTDIR=\"" + dest.string() +
        "\" PREFIX=/usr/local >/dev/null 2>&1");
}

RUN_ALL_TESTS()