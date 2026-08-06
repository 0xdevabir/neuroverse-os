// neuro/core/result.hpp
//
// A std::expected-based Result<T> type with rich error context.
// Mirrors README §9.1 exactly so the rest of the starter scaffold compiles.

#pragma once

#include <concepts>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace neuro::core {

enum class ErrorKind {
    None,
    InvalidArgument,
    OutOfMemory,
    Unreachable,
    NotPermitted,
    NotFound,
    Timeout,
    WouldBlock,
    Internal,
    External,
    Unknown,
};

struct Error {
    ErrorKind   kind;
    int         code;
    std::string message;
    std::string location;

    [[nodiscard]] static Error make(ErrorKind k, int c, std::string msg,
                                    std::string loc = {}) {
        return Error{k, c, std::move(msg), std::move(loc)};
    }
};

template <class T>
using Result = std::expected<T, Error>;

struct Unit {};

// Trait: is_result
template <class T>
struct is_result : std::false_type {};
template <class T>
struct is_result<Result<T>> : std::true_type {};
template <>
struct is_result<Result<Unit>> : std::true_type {};

template <class T>
concept AnyResult = is_result<std::remove_cvref_t<T>>::value;

inline const char* to_string(ErrorKind k) noexcept {
    switch (k) {
        case ErrorKind::None:           return "none";
        case ErrorKind::InvalidArgument:return "invalid_argument";
        case ErrorKind::OutOfMemory:    return "out_of_memory";
        case ErrorKind::Unreachable:    return "unreachable";
        case ErrorKind::NotPermitted:   return "not_permitted";
        case ErrorKind::NotFound:       return "not_found";
        case ErrorKind::Timeout:        return "timeout";
        case ErrorKind::WouldBlock:     return "would_block";
        case ErrorKind::Internal:       return "internal";
        case ErrorKind::External:       return "external";
        case ErrorKind::Unknown:        return "unknown";
    }
    return "unknown";
}

}  // namespace neuro::core