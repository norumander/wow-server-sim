#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "server/telemetry/logger.h"

using json = nlohmann::json;

/// Test fixture that configures the logger with a custom ostringstream sink
/// and ensures cleanup after every test.
class LoggerTest : public ::testing::Test {
protected:
    std::ostringstream sink_;

    void SetUp() override
    {
        // Ensure clean state before each test.
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    void TearDown() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    /// Initialize logger with the test sink only.
    void init_with_sink()
    {
        wow::LoggerConfig config;
        config.custom_sink = &sink_;
        wow::Logger::initialize(config);
    }

    /// Parse all JSON lines from the sink into a vector.
    std::vector<json> parse_all_lines()
    {
        std::vector<json> entries;
        std::istringstream stream(sink_.str());
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                entries.push_back(json::parse(line));
            }
        }
        return entries;
    }

    /// Parse the single JSON line from the sink.
    json parse_single_line()
    {
        auto entries = parse_all_lines();
        EXPECT_EQ(entries.size(), 1u) << "Expected exactly one log line";
        return entries.empty() ? json::object() : entries[0];
    }
};

// ---------------------------------------------------------------------------
// Lifecycle tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, IsInitializedReturnsFalseBeforeInitialize)
{
    EXPECT_FALSE(wow::Logger::is_initialized());
}

TEST_F(LoggerTest, IsInitializedReturnsTrueAfterInitialize)
{
    init_with_sink();
    EXPECT_TRUE(wow::Logger::is_initialized());
}

TEST_F(LoggerTest, IsInitializedReturnsFalseAfterReset)
{
    init_with_sink();
    wow::Logger::reset();
    EXPECT_FALSE(wow::Logger::is_initialized());
}

TEST_F(LoggerTest, InstanceThrowsWhenNotInitialized)
{
    EXPECT_THROW(wow::Logger::instance(), std::runtime_error);
}

TEST_F(LoggerTest, DoubleInitializeThrows)
{
    init_with_sink();
    wow::LoggerConfig config;
    EXPECT_THROW(wow::Logger::initialize(config), std::runtime_error);
}

TEST_F(LoggerTest, ResetThenReinitializeSucceeds)
{
    init_with_sink();
    wow::Logger::reset();
    // Reinitialize with a fresh sink — should not throw.
    std::ostringstream fresh_sink;
    wow::LoggerConfig config;
    config.custom_sink = &fresh_sink;
    EXPECT_NO_THROW(wow::Logger::initialize(config));
}

// ---------------------------------------------------------------------------
// Schema compliance tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, LogEntryIsValidJson)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "test", "hello");
    std::string output = sink_.str();
    ASSERT_FALSE(output.empty()) << "Logger produced no output";
    EXPECT_NO_THROW((void)json::parse(output));
}

TEST_F(LoggerTest, LogEntryContainsSchemaVersion)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "test", "hello");
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("v"));
    EXPECT_EQ(entry["v"], 1);
}

TEST_F(LoggerTest, LogEntryContainsTimestampInIso8601)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "test", "hello");
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("timestamp"));
    std::string ts = entry["timestamp"];

    // Matches: YYYY-MM-DDTHH:MM:SS.mmmZ
    std::regex iso_pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)");
    EXPECT_TRUE(std::regex_match(ts, iso_pattern))
        << "Timestamp not in expected ISO 8601 format: " << ts;
}

TEST_F(LoggerTest, LogEntryContainsTypeField)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "test", "hello");
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("type"));
    EXPECT_EQ(entry["type"], "event");
}

TEST_F(LoggerTest, LogEntryContainsComponentField)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "session", "hello");
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("component"));
    EXPECT_EQ(entry["component"], "session");
}

TEST_F(LoggerTest, LogEntryContainsMessageField)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "test", "Player connected");
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("message"));
    EXPECT_EQ(entry["message"], "Player connected");
}

// ---------------------------------------------------------------------------
// LogType mapping tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, LogTypeMetricProducesCorrectString)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::metric, "perf", "tick");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "metric");
}

TEST_F(LoggerTest, LogTypeEventProducesCorrectString)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::event, "session", "connect");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "event");
}

TEST_F(LoggerTest, LogTypeHealthProducesCorrectString)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::health, "zone", "ok");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "health");
}

TEST_F(LoggerTest, LogTypeErrorProducesCorrectString)
{
    init_with_sink();
    wow::Logger::instance().log(wow::LogType::error, "combat", "null ref");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "error");
}

// ---------------------------------------------------------------------------
// Convenience method tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, MetricConvenienceSetsTypeMetric)
{
    init_with_sink();
    wow::Logger::instance().metric("perf", "tick_duration", {{"ms", 48}});
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "metric");
    EXPECT_EQ(entry["component"], "perf");
}

TEST_F(LoggerTest, EventConvenienceSetsTypeEvent)
{
    init_with_sink();
    wow::Logger::instance().event("session", "connected");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "event");
    EXPECT_EQ(entry["component"], "session");
}

TEST_F(LoggerTest, HealthConvenienceSetsTypeHealth)
{
    init_with_sink();
    wow::Logger::instance().health("zone", "zone status");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "health");
    EXPECT_EQ(entry["component"], "zone");
}

TEST_F(LoggerTest, ErrorConvenienceSetsTypeError)
{
    init_with_sink();
    wow::Logger::instance().error("combat", "null target");
    auto entry = parse_single_line();
    EXPECT_EQ(entry["type"], "error");
    EXPECT_EQ(entry["component"], "combat");
}

// ---------------------------------------------------------------------------
// Data handling tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, DataFieldContainsProvidedPayload)
{
    init_with_sink();
    json payload = {{"session_id", 42}, {"player", "Thrall"}};
    wow::Logger::instance().event("session", "connected", payload);
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("data"));
    EXPECT_EQ(entry["data"]["session_id"], 42);
    EXPECT_EQ(entry["data"]["player"], "Thrall");
}

TEST_F(LoggerTest, EmptyDataDefaultProducesEmptyObject)
{
    init_with_sink();
    wow::Logger::instance().event("session", "ping");
    auto entry = parse_single_line();
    // Either no "data" key or an empty object is acceptable.
    if (entry.contains("data")) {
        EXPECT_TRUE(entry["data"].empty());
    }
}

TEST_F(LoggerTest, NestedDataStructuresArePreserved)
{
    init_with_sink();
    json nested = {{"position", {{"x", 1.5}, {"y", 2.5}, {"z", 0.0}}}};
    wow::Logger::instance().event("movement", "update", nested);
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("data"));
    EXPECT_DOUBLE_EQ(entry["data"]["position"]["x"].get<double>(), 1.5);
    EXPECT_DOUBLE_EQ(entry["data"]["position"]["y"].get<double>(), 2.5);
}

TEST_F(LoggerTest, DataWithArrayValues)
{
    init_with_sink();
    json payload = {{"targets", {1, 2, 3}}};
    wow::Logger::instance().event("combat", "aoe", payload);
    auto entry = parse_single_line();
    ASSERT_TRUE(entry.contains("data"));
    auto targets = entry["data"]["targets"];
    ASSERT_EQ(targets.size(), 3u);
    EXPECT_EQ(targets[0], 1);
}

// ---------------------------------------------------------------------------
// Multi-line output tests
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, MultipleLogCallsProduceSeparateLines)
{
    init_with_sink();
    wow::Logger::instance().event("a", "first");
    wow::Logger::instance().event("b", "second");
    wow::Logger::instance().event("c", "third");

    auto entries = parse_all_lines();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0]["message"], "first");
    EXPECT_EQ(entries[1]["message"], "second");
    EXPECT_EQ(entries[2]["message"], "third");
}

TEST_F(LoggerTest, EachLineIsIndependentlyParseable)
{
    init_with_sink();
    wow::Logger::instance().metric("perf", "tick", {{"ms", 50}});
    wow::Logger::instance().error("zone", "crash", {{"zone_id", 1}});

    std::istringstream stream(sink_.str());
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            EXPECT_NO_THROW((void)json::parse(line)) << "Line not valid JSON: " << line;
            count++;
        }
    }
    EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// File output test
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, FileOutputReceivesJsonLines)
{
    // Use a temp file path.
    std::string tmp_path = std::filesystem::temp_directory_path().string() +
                           "/wow_logger_test_" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()) +
                           ".jsonl";

    wow::LoggerConfig config;
    config.file_path = tmp_path;
    wow::Logger::initialize(config);
    wow::Logger::instance().event("test", "file write", {{"key", "value"}});
    wow::Logger::reset();

    // Read back and verify.
    std::ifstream file(tmp_path);
    ASSERT_TRUE(file.is_open()) << "Log file not created: " << tmp_path;
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    auto entry = json::parse(line);
    EXPECT_EQ(entry["message"], "file write");
    EXPECT_EQ(entry["data"]["key"], "value");
    file.close();

    // Cleanup.
    std::filesystem::remove(tmp_path);
}

// ---------------------------------------------------------------------------
// Thread safety test
// ---------------------------------------------------------------------------

TEST_F(LoggerTest, ConcurrentLogCallsDoNotCorruptOutput)
{
    init_with_sink();
    constexpr int threads_count = 4;
    constexpr int logs_per_thread = 50;

    auto worker = [logs_per_thread](int id) {
        for (int i = 0; i < logs_per_thread; ++i) {
            wow::Logger::instance().event(
                "thread", "msg",
                {{"thread_id", id}, {"seq", i}});
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(threads_count);
    for (int t = 0; t < threads_count; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto entries = parse_all_lines();
    EXPECT_EQ(entries.size(), static_cast<size_t>(threads_count * logs_per_thread));

    // Every entry must be valid JSON with expected fields.
    for (const auto& e : entries) {
        EXPECT_TRUE(e.contains("v"));
        EXPECT_TRUE(e.contains("timestamp"));
        EXPECT_TRUE(e.contains("type"));
    }
}
