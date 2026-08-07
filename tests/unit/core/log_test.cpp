// tests/unit/core/log_test.cpp
//
// Phase P2.1 — structured logging primitives.

#include "tests/test_framework.hpp"

#include "neuro/core/log.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace neuro::core::log;

namespace {

struct SinkGuard {
    SinkGuard()  { prev_ = set_sink(&queue_); }
    ~SinkGuard() { set_sink(prev_); }
    QueueSink  queue_;
    Sink*      prev_ = nullptr;
};

}  // namespace

TEST(log, level_name_strings) {
    EXPECT_EQ(std::string("TRACE"), std::string(level_name(Level::Trace)));
    EXPECT_EQ(std::string("DEBUG"), std::string(level_name(Level::Debug)));
    EXPECT_EQ(std::string("INFO"),  std::string(level_name(Level::Info)));
    EXPECT_EQ(std::string("WARN"),  std::string(level_name(Level::Warn)));
    EXPECT_EQ(std::string("ERROR"), std::string(level_name(Level::Error)));
}

TEST(log, level_filter_drops_records_below_threshold) {
    SinkGuard g;
    set_level(Level::Warn);

    emit(make_record(Level::Info, "should_be_dropped"));
    EXPECT_EQ(0u, g.queue_.size());

    emit(make_record(Level::Error, "should_be_kept"));
    EXPECT_EQ(1u, g.queue_.size());
    EXPECT_EQ(Level::Error, g.queue_.snapshot().back().level);
}

TEST(log, structured_fields_round_trip) {
    SinkGuard g;
    set_level(Level::Trace);

    NEURO_LOG_INFO("hello",
                   NEURO_FIELD("count", 7LL),
                   NEURO_FIELD("name", std::string("world")),
                   NEURO_FIELD("ratio", 1.5),
                   NEURO_FIELD("ok", true));
    auto records = g.queue_.snapshot();
    EXPECT_EQ(1u, records.size());
    auto& fields = records[0].fields;
    EXPECT_EQ(4u, fields.size());
    auto it = fields.find("count");
    EXPECT_TRUE(it != fields.end());
    EXPECT_EQ(7LL, std::get<long long>(it->second));
    it = fields.find("name");
    EXPECT_TRUE(it != fields.end());
    EXPECT_EQ(std::string("world"), std::get<std::string>(it->second));
    it = fields.find("ratio");
    EXPECT_TRUE(it != fields.end());
    EXPECT_EQ(1.5, std::get<double>(it->second));
    it = fields.find("ok");
    EXPECT_TRUE(it != fields.end());
    EXPECT_TRUE(std::get<bool>(it->second));
}

TEST(log, source_location_captured_at_call_site) {
    SinkGuard g;
    set_level(Level::Trace);

    auto loc = std::source_location::current();
    NEURO_LOG_INFO("from-here");
    auto records = g.queue_.snapshot();
    EXPECT_EQ(1u, records.size());
    EXPECT_EQ(loc.line() + 1, records[0].location.line());
    EXPECT_TRUE(std::strcmp(records[0].location.file_name(),
                            __FILE__) == 0);
}

TEST(log, set_sink_returns_previous_sink) {
    SinkGuard outer;
    QueueSink local;
    auto* prev = set_sink(&local);
    EXPECT_TRUE(prev == &outer.queue_);
    set_sink(prev);
}

TEST(log, null_sink_swallows_records) {
    NullSink null;
    null.emit(make_record(Level::Error, "noop"));
    EXPECT_EQ(0u, 0u);
}

TEST(log, queue_sink_thread_safe_under_producers) {
    SinkGuard g;
    set_level(Level::Trace);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i) {
                NEURO_LOG_DEBUG("worker",
                                NEURO_FIELD("tid", t),
                                NEURO_FIELD("i", i));
            }
        });
    }
    for (auto& w : workers) w.join();
    EXPECT_EQ(static_cast<std::size_t>(kThreads * kPerThread),
              g.queue_.size());
}

TEST(log, record_to_string_includes_level_and_message) {
    SinkGuard g;
    set_level(Level::Trace);
    NEURO_LOG_WARN("oops", NEURO_FIELD("status", 500LL));
    auto records = g.queue_.snapshot();
    EXPECT_EQ(1u, records.size());
    std::string s = records[0].to_string();
    EXPECT_TRUE(s.find("[WARN]") != std::string::npos);
    EXPECT_TRUE(s.find("oops") != std::string::npos);
    EXPECT_TRUE(s.find("status=500") != std::string::npos);
}

TEST(log, trace_and_debug_dropped_at_info_level) {
    SinkGuard g;
    set_level(Level::Info);
    NEURO_LOG_TRACE("t");
    NEURO_LOG_DEBUG("d");
    EXPECT_EQ(0u, g.queue_.size());
    NEURO_LOG_INFO("i");
    EXPECT_EQ(1u, g.queue_.size());
}

TEST(log, char_string_literal_field_value) {
    SinkGuard g;
    set_level(Level::Trace);
    NEURO_LOG_INFO("hello", NEURO_FIELD("who", "world"));
    auto records = g.queue_.snapshot();
    EXPECT_EQ(1u, records.size());
    auto it = records[0].fields.find("who");
    EXPECT_TRUE(it != records[0].fields.end());
    EXPECT_EQ(std::string("world"), std::get<std::string>(it->second));
}

TEST(log, no_fields_compiles_clean) {
    SinkGuard g;
    set_level(Level::Trace);
    NEURO_LOG_INFO("just-a-message");
    EXPECT_EQ(1u, g.queue_.size());
    EXPECT_TRUE(g.queue_.snapshot()[0].fields.empty());
}

RUN_ALL_TESTS()
