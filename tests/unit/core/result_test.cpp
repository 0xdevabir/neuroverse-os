// tests/unit/core/result_test.cpp
//
// Tests for neuro::core::Result<T> — the std::expected-based result
// type used by every subsystem.
//
// Coverage:
//   - success Result<int> holds value
//   - failure Result<int> holds Error with all fields
//   - Error::make() default / explicit location
//   - Result<Unit> success
//   - is_result trait and AnyResult concept
//   - ErrorKind::to_string() maps every enum value

#include "neuro/core/result.hpp"

#include <string>
#include <type_traits>

#include "../../test_framework.hpp"

using neuro::core::AnyResult;
using neuro::core::Error;
using neuro::core::ErrorKind;
using neuro::core::Result;
using neuro::core::Unit;
using neuro::core::is_result;
using neuro::core::to_string;

// ---- 1. success result --------------------------------------------

TEST(result, success_holds_value) {
    Result<int> r = 42;
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(42, r.value());
    EXPECT_EQ(42, *r);
}

TEST(result, success_string) {
    Result<std::string> r = std::string("neuro");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(std::string("neuro"), r.value());
}

// ---- 2. failure result --------------------------------------------

TEST(result, failure_holds_error) {
    Error e = Error::make(ErrorKind::NotFound, 404,
                          "object not found", "store.cpp:42");
    Result<int> r = std::unexpected(e);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(ErrorKind::NotFound, r.error().kind);
    EXPECT_EQ(404, r.error().code);
    EXPECT_EQ(std::string("object not found"), r.error().message);
    EXPECT_EQ(std::string("store.cpp:42"), r.error().location);
}

TEST(result, error_make_default_location) {
    auto e = Error::make(ErrorKind::Timeout, 110, "deadline exceeded");
    EXPECT_EQ(ErrorKind::Timeout, e.kind);
    EXPECT_EQ(110, e.code);
    EXPECT_EQ(std::string("deadline exceeded"), e.message);
    EXPECT_TRUE(e.location.empty());
}

// ---- 3. Unit result -----------------------------------------------

TEST(result, unit_success) {
    Result<Unit> r = Unit{};
    EXPECT_TRUE(r.has_value());
}

TEST(result, unit_failure) {
    Result<Unit> r = std::unexpected(
        Error::make(ErrorKind::NotPermitted, 1, "denied"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(ErrorKind::NotPermitted, r.error().kind);
}

// ---- 4. traits / concept -----------------------------------------

TEST(result, is_result_trait) {
    static_assert(is_result<Result<int>>::value);
    static_assert(is_result<Result<Unit>>::value);
    static_assert(!is_result<int>::value);
    static_assert(!is_result<std::string>::value);
    EXPECT_TRUE((is_result<Result<double>>::value));
}

namespace {

template <AnyResult R>
constexpr bool accepts_result(const R&) { return true; }

}  // namespace

TEST(result, any_result_concept) {
    Result<int> r = 1;
    Result<Unit> u = Unit{};
    EXPECT_TRUE(accepts_result(r));
    EXPECT_TRUE(accepts_result(u));
}

// ---- 5. ErrorKind strings ----------------------------------------

TEST(result, error_kind_strings) {
    EXPECT_EQ(std::string("none"),             to_string(ErrorKind::None));
    EXPECT_EQ(std::string("invalid_argument"), to_string(ErrorKind::InvalidArgument));
    EXPECT_EQ(std::string("out_of_memory"),    to_string(ErrorKind::OutOfMemory));
    EXPECT_EQ(std::string("unreachable"),      to_string(ErrorKind::Unreachable));
    EXPECT_EQ(std::string("not_permitted"),    to_string(ErrorKind::NotPermitted));
    EXPECT_EQ(std::string("not_found"),        to_string(ErrorKind::NotFound));
    EXPECT_EQ(std::string("timeout"),          to_string(ErrorKind::Timeout));
    EXPECT_EQ(std::string("would_block"),      to_string(ErrorKind::WouldBlock));
    EXPECT_EQ(std::string("internal"),         to_string(ErrorKind::Internal));
    EXPECT_EQ(std::string("external"),         to_string(ErrorKind::External));
    EXPECT_EQ(std::string("unknown"),          to_string(ErrorKind::Unknown));
}

TEST(result, invalid_error_kind_string_falls_back_unknown) {
    auto bad = static_cast<ErrorKind>(999);
    EXPECT_EQ(std::string("unknown"), to_string(bad));
}

RUN_ALL_TESTS()
