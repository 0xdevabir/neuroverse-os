// neuro/core/log.hpp
//
// Phase P2.1 — structured logging primitives.
// Phase P2.2 — scoped sink and context RAII helpers.
//
// A small, dependency-free logging facility. Key properties:
//
//   * Levels: Trace, Debug, Info, Warn, Error.
//   * Records carry source location, structured key/value fields,
//     and a free-form message.
//   * Sinks are pluggable; the default is a thread-safe StderrSink.
//     Tests can substitute a QueueSink for deterministic capture.
//   * No background thread by default. Sinks run on the thread that
//     produced the record; flush() is synchronous.
//   * ScopedLogSink isolates output redirection.
//   * ScopedLogContext attaches thread-local fields to every record
//     emitted inside the scope.
//
// Use:
//   NEURO_LOG_INFO("server ready");
//   NEURO_LOG_ERROR("bind failed",
//                   NEURO_FIELD("port", 8080),
//                   NEURO_FIELD("errno", 13));
//
// NEURO_FIELD wraps a key/value pair into a typed Field<T> that the
// log macro folds into the record. Each supported value type is
// listed in field_value() below.

#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace neuro::core::log {

enum class Level : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

inline const char* level_name(Level l) noexcept {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

// ---- Field value -------------------------------------------------------

using FieldValue = std::variant<std::string, long long, double, bool>;

inline FieldValue field_value(std::string_view x) { return std::string(x); }
inline FieldValue field_value(const char* x)      { return std::string(x); }
inline FieldValue field_value(std::string&& x)    { return std::move(x); }
inline FieldValue field_value(const std::string& x) { return x; }
inline FieldValue field_value(long long x)        { return x; }
inline FieldValue field_value(int x)              { return static_cast<long long>(x); }
inline FieldValue field_value(double x)           { return x; }
inline FieldValue field_value(bool x)             { return x; }

inline std::string field_value_to_string(const FieldValue& v) {
    std::ostringstream s;
    std::visit([&](auto&& x) { s << x; }, v);
    return s.str();
}

// A typed field. Tag::Type isn't strictly required by the runtime,
// but it keeps the type system honest about which overload of
// field_value() will be called.
template <typename T>
struct Field {
    std::string key;
    T           value;
};

template <typename T>
inline Field<T> field(std::string_view key, T&& value) {
    return Field<T>{std::string(key), std::forward<T>(value)};
}

// ---- Record -------------------------------------------------------------

struct Record {
    Level                             level = Level::Info;
    std::string                       message;
    std::map<std::string, FieldValue> fields;
    std::source_location              location;

    [[nodiscard]] std::string to_string() const {
        std::ostringstream s;
        s << '[' << level_name(level) << "] "
          << location.file_name() << ':' << location.line() << ' '
          << message;
        if (!fields.empty()) {
            s << " {";
            bool first = true;
            for (const auto& [k, v] : fields) {
                if (!first) s << ", ";
                first = false;
                s << k << '=' << field_value_to_string(v);
            }
            s << '}';
        }
        return s.str();
    }
};

// ---- Sinks -------------------------------------------------------------

class Sink {
public:
    virtual ~Sink() = default;
    virtual void emit(const Record& record) = 0;
    virtual void flush() noexcept {}
};

class NullSink final : public Sink {
public:
    void emit(const Record&) override {}
};

class StderrSink final : public Sink {
public:
    void emit(const Record& record) override {
        std::fputs(record.to_string().c_str(), stderr);
        std::fputc('\n', stderr);
    }
};

class QueueSink final : public Sink {
public:
    void emit(const Record& record) override {
        std::lock_guard<std::mutex> g(mu_);
        records_.push_back(record);
    }
    void clear() {
        std::lock_guard<std::mutex> g(mu_);
        records_.clear();
    }
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> g(mu_);
        return records_.size();
    }
    [[nodiscard]] std::vector<Record> snapshot() const {
        std::lock_guard<std::mutex> g(mu_);
        return {records_.begin(), records_.end()};
    }

private:
    mutable std::mutex   mu_;
    std::deque<Record>   records_;
};

// ---- Global sink + level ----------------------------------------------

namespace detail {

inline Sink*& global_sink_ref() noexcept {
    static Sink* s = nullptr;
    return s;
}

inline Sink* global_sink() noexcept {
    if (!global_sink_ref()) global_sink_ref() = new StderrSink();
    return global_sink_ref();
}

inline Level& global_level_ref() noexcept {
    static Level lvl = Level::Info;
    return lvl;
}

}  // namespace detail

inline Sink* set_sink(Sink* sink) noexcept {
    Sink* previous = detail::global_sink_ref();
    detail::global_sink_ref() = sink ? sink : new StderrSink();
    return previous;
}

inline void set_level(Level lvl) noexcept {
    detail::global_level_ref() = lvl;
}

inline Level level() noexcept {
    return detail::global_level_ref();
}

inline void emit(Record record) {
    if (static_cast<std::uint8_t>(record.level) <
        static_cast<std::uint8_t>(detail::global_level_ref())) {
        return;
    }
    detail::global_sink()->emit(record);
}

inline Record make_record(Level lvl, std::string_view msg,
                          std::source_location loc =
                              std::source_location::current()) {
    return Record{lvl, std::string(msg), {}, loc};
}

// Apply a single typed Field<T> into the record. Each supported T
// has its value converted into FieldValue via field_value().
template <typename T>
inline void add_field(Record& r, const Field<T>& f) {
    r.fields[std::move(f.key)] = field_value(f.value);
}

// Fold any number of fields into a record using C++17 fold syntax.
template <typename... Fields>
inline Record with_fields(Record record, Fields&&... fields) {
    (add_field(record, std::forward<Fields>(fields)), ...);
    return record;
}

// ---- Phase P2.2 — scoped sink and context -----------------------------

class ScopedLogSink {
public:
    explicit ScopedLogSink(Sink* sink) noexcept
        : previous_(set_sink(sink)) {}
    ~ScopedLogSink() { set_sink(previous_); }
    ScopedLogSink(const ScopedLogSink&)            = delete;
    ScopedLogSink& operator=(const ScopedLogSink&) = delete;

private:
    Sink* previous_;
};

class ScopedLogContext {
public:
    explicit ScopedLogContext(std::string key, FieldValue value)
        : previous_count_(context_stack().size()) {
        context_stack().emplace_back(std::move(key), std::move(value));
    }
    ~ScopedLogContext() {
        // Restore previous size even if other ScopedLogContexts were
        // pushed inside this scope. We do not free the popped records
        // because the underlying vector storage persists for the
        // thread's lifetime; only the size is restored.
        if (context_stack().size() > previous_count_) {
            context_stack().resize(previous_count_);
        }
    }
    ScopedLogContext(const ScopedLogContext&)            = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

    // Internal accessor; storage remains thread-local to the calling
    // thread and is never shared between producers.
    static std::vector<std::pair<std::string, FieldValue>>&
    context_stack() {
        thread_local std::vector<std::pair<std::string, FieldValue>> stack;
        return stack;
    }

private:
    std::size_t previous_count_;
};

inline Record with_context(Record record) {
    for (const auto& [k, v] : ScopedLogContext::context_stack()) {
        record.fields[k] = v;
    }
    return record;
}

}  // namespace neuro::core::log

// ---- Macros ------------------------------------------------------------
//
// Use NEURO_LOG_<level>(message [, NEURO_FIELD(k,v), ...]).
// Level is one of TRACE, DEBUG, INFO, WARN, ERROR.

#define NEURO_FIELD(key, value) ::neuro::core::log::field(key, value)

#define NEURO_LOG_IMPL(lvl, msg, ...)                                       \
    do {                                                                    \
        auto __neuro_record = ::neuro::core::log::make_record(              \
            ::neuro::core::log::Level::lvl, msg,                            \
            std::source_location::current());                               \
        __neuro_record = ::neuro::core::log::with_fields(                   \
            std::move(__neuro_record) __VA_OPT__(,) __VA_ARGS__);            \
        __neuro_record = ::neuro::core::log::with_context(                  \
            std::move(__neuro_record));                                     \
        ::neuro::core::log::emit(std::move(__neuro_record));                \
    } while (0)

#define NEURO_LOG_TRACE(msg, ...) NEURO_LOG_IMPL(Trace, msg, __VA_ARGS__)
#define NEURO_LOG_DEBUG(msg, ...) NEURO_LOG_IMPL(Debug, msg, __VA_ARGS__)
#define NEURO_LOG_INFO(msg, ...)  NEURO_LOG_IMPL(Info,  msg, __VA_ARGS__)
#define NEURO_LOG_WARN(msg, ...)  NEURO_LOG_IMPL(Warn,  msg, __VA_ARGS__)
#define NEURO_LOG_ERROR(msg, ...) NEURO_LOG_IMPL(Error, msg, __VA_ARGS__)