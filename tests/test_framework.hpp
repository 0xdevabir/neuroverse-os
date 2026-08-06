// tests/test_framework.hpp
//
// Tiny single-header test framework used across NeuroVerse OS tests.
// Returns a non-zero exit code if any check fails.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace neuro::testing {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

inline int run_all() {
    int failed = 0;
    for (const auto& t : registry()) {
        std::printf("[ RUN      ] %s\n", t.name);
        try {
            t.fn();
            std::printf("[       OK ] %s\n", t.name);
        } catch (const std::exception& e) {
            std::printf("[  FAILED  ] %s: %s\n", t.name, e.what());
            ++failed;
        } catch (...) {
            std::printf("[  FAILED  ] %s: unknown exception\n", t.name);
            ++failed;
        }
    }
    std::printf("\n=========================================\n");
    if (failed == 0) {
        std::printf("[  PASSED  ] all %zu tests\n", registry().size());
        return 0;
    } else {
        std::printf("[  FAILED  ] %d of %zu tests\n",
                    failed, registry().size());
        return 1;
    }
}

}  // namespace neuro::testing

// ---- Test assertion macros ---------------------------------------------

#define EXPECT_TRUE(cond)                                                 \
    do {                                                                  \
        if (!(cond)) {                                                    \
            throw std::runtime_error(std::string(__FILE__) + ":" +        \
                                     std::to_string(__LINE__) +           \
                                     " EXPECT_TRUE failed: " #cond);      \
        }                                                                 \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                   \
    do {                                                                  \
        auto _a = (a);                                                    \
        auto _b = (b);                                                    \
        if (!(_a == _b)) {                                                \
            throw std::runtime_error(std::string(__FILE__) + ":" +        \
                                     std::to_string(__LINE__) +           \
                                     " EXPECT_EQ failed: " #a " == " #b); \
        }                                                                 \
    } while (0)

#define EXPECT_NE(a, b)                                                   \
    do {                                                                  \
        auto _a = (a);                                                    \
        auto _b = (b);                                                    \
        if (!(_a != _b)) {                                                \
            throw std::runtime_error(std::string(__FILE__) + ":" +        \
                                     std::to_string(__LINE__) +           \
                                     " EXPECT_NE failed: " #a " != " #b); \
        }                                                                 \
    } while (0)

// ---- Test registration -------------------------------------------------

#define TEST(suite, name)                                                 \
    static void suite##_##name();                                         \
    static ::neuro::testing::Registrar                                    \
        registrar_##suite##_##name(#suite "." #name, &suite##_##name);    \
    static void suite##_##name()

#define RUN_ALL_TESTS()                                                   \
    int main() { return ::neuro::testing::run_all(); }