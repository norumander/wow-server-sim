#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "server/game_loop.h"
#include "server/telemetry/logger.h"

using json = nlohmann::json;

// ===========================================================================
// Test fixture — initializes Logger with ostringstream sink for telemetry
// tests. Construction-only tests (Group A) don't need the logger.
// ===========================================================================

class GameLoopTest : public ::testing::Test {
protected:
    std::ostringstream sink_;

    void SetUp() override
    {
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

    /// Initialize the telemetry logger with our test sink.
    void init_logger()
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

    /// Filter log entries by type and component.
    std::vector<json> filter_entries(const std::string& type,
                                     const std::string& component)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            if (entry.value("type", "") == type &&
                entry.value("component", "") == component) {
                result.push_back(entry);
            }
        }
        return result;
    }
};

// ===========================================================================
// Group A: Construction & Config (no Logger needed)
// ===========================================================================

TEST_F(GameLoopTest, DefaultConfigUses20Hz)
{
    wow::GameLoop loop;
    auto expected = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / 20.0));
    EXPECT_EQ(loop.tick_interval(), expected);
}

TEST_F(GameLoopTest, CustomTickRateIsRespected)
{
    wow::GameLoopConfig config;
    config.tick_rate_hz = 10.0;
    wow::GameLoop loop(config);
    auto expected = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / 10.0));
    EXPECT_EQ(loop.tick_interval(), expected);
}

TEST_F(GameLoopTest, IsNotRunningAfterConstruction)
{
    wow::GameLoop loop;
    EXPECT_FALSE(loop.is_running());
}

TEST_F(GameLoopTest, TickCountIsZeroAfterConstruction)
{
    wow::GameLoop loop;
    EXPECT_EQ(loop.tick_count(), 0u);
}

// ===========================================================================
// Group B: Start/Stop Lifecycle
// ===========================================================================

TEST_F(GameLoopTest, StartSetsRunningTrue)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;
    wow::GameLoop loop(config);

    loop.start();
    EXPECT_TRUE(loop.is_running());
    loop.stop();
}

TEST_F(GameLoopTest, StopSetsRunningFalse)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;
    wow::GameLoop loop(config);

    loop.start();
    loop.stop();
    EXPECT_FALSE(loop.is_running());
}

TEST_F(GameLoopTest, StopIsIdempotent)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;
    wow::GameLoop loop(config);

    loop.start();
    loop.stop();
    // Second stop should not crash or hang.
    loop.stop();
    EXPECT_FALSE(loop.is_running());
}

TEST_F(GameLoopTest, DestructorStopsRunningLoop)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;

    {
        wow::GameLoop loop(config);
        loop.start();
        EXPECT_TRUE(loop.is_running());
        // Destructor fires here — should stop cleanly without hanging.
    }
    // If we get here, the destructor didn't hang.
    SUCCEED();
}

// ===========================================================================
// Group C: Tick Execution
// ===========================================================================

TEST_F(GameLoopTest, TickCountIncrementsWhileRunning)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 1000.0;  // Fast ticks for quick test
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    EXPECT_GT(loop.tick_count(), 0u);
}

TEST_F(GameLoopTest, CallbackIsInvokedOnEachTick)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 1000.0;
    wow::GameLoop loop(config);

    std::atomic<int> counter{0};
    loop.on_tick([&counter](uint64_t) { counter++; });

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    EXPECT_GT(counter.load(), 0);
}

TEST_F(GameLoopTest, CallbackReceivesSequentialTickNumbers)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 1000.0;
    wow::GameLoop loop(config);

    std::vector<uint64_t> recorded_ticks;
    std::mutex mtx;
    loop.on_tick([&recorded_ticks, &mtx](uint64_t tick) {
        std::lock_guard<std::mutex> lock(mtx);
        recorded_ticks.push_back(tick);
    });

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_GT(recorded_ticks.size(), 2u);
    for (size_t i = 0; i < recorded_ticks.size(); ++i) {
        EXPECT_EQ(recorded_ticks[i], i)
            << "Tick number mismatch at index " << i;
    }
}

TEST_F(GameLoopTest, MultipleCallbacksAllInvoked)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 1000.0;
    wow::GameLoop loop(config);

    std::atomic<int> counter_a{0};
    std::atomic<int> counter_b{0};
    loop.on_tick([&counter_a](uint64_t) { counter_a++; });
    loop.on_tick([&counter_b](uint64_t) { counter_b++; });

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    EXPECT_GT(counter_a.load(), 0);
    EXPECT_GT(counter_b.load(), 0);
    // Both should be invoked the same number of times.
    EXPECT_EQ(counter_a.load(), counter_b.load());
}

// ===========================================================================
// Group D: Telemetry Emission
// ===========================================================================

TEST_F(GameLoopTest, EmitsStartEvent)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop.stop();

    auto events = filter_entries("event", "game_loop");
    bool found_start = false;
    for (const auto& e : events) {
        if (e.value("message", "").find("started") != std::string::npos) {
            found_start = true;
            break;
        }
    }
    EXPECT_TRUE(found_start) << "Expected a 'started' event from game_loop";
}

TEST_F(GameLoopTest, EmitsStopEvent)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop.stop();

    auto events = filter_entries("event", "game_loop");
    bool found_stop = false;
    for (const auto& e : events) {
        if (e.value("message", "").find("stopped") != std::string::npos) {
            found_stop = true;
            // Verify total_ticks is reported.
            ASSERT_TRUE(e.contains("data"));
            EXPECT_TRUE(e["data"].contains("total_ticks"));
            break;
        }
    }
    EXPECT_TRUE(found_stop) << "Expected a 'stopped' event from game_loop";
}

TEST_F(GameLoopTest, EmitsTickMetricPerTick)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 500.0;
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    auto metrics = filter_entries("metric", "game_loop");
    EXPECT_GT(metrics.size(), 0u) << "Expected at least one tick metric";
}

TEST_F(GameLoopTest, TickMetricContainsDurationAndTick)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 500.0;
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    auto metrics = filter_entries("metric", "game_loop");
    ASSERT_GT(metrics.size(), 0u);

    const auto& m = metrics[0];
    ASSERT_TRUE(m.contains("data"));
    EXPECT_TRUE(m["data"].contains("duration_ms"))
        << "Tick metric missing duration_ms";
    EXPECT_TRUE(m["data"].contains("tick"))
        << "Tick metric missing tick number";
}

TEST_F(GameLoopTest, TickMetricContainsOverrunFlag)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 500.0;
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();

    auto metrics = filter_entries("metric", "game_loop");
    ASSERT_GT(metrics.size(), 0u);

    const auto& m = metrics[0];
    ASSERT_TRUE(m.contains("data"));
    EXPECT_TRUE(m["data"].contains("overrun"))
        << "Tick metric missing overrun flag";
    EXPECT_TRUE(m["data"]["overrun"].is_boolean())
        << "overrun should be a boolean";
}

// ===========================================================================
// Group E: Overrun Detection
// ===========================================================================

TEST_F(GameLoopTest, NormalTickReportsNoOverrun)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 10.0;  // 100ms per tick — plenty of headroom
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop.stop();

    auto metrics = filter_entries("metric", "game_loop");
    ASSERT_GT(metrics.size(), 0u);

    for (const auto& m : metrics) {
        ASSERT_TRUE(m.contains("data"));
        EXPECT_FALSE(m["data"]["overrun"].get<bool>())
            << "Empty callback should not overrun at 10 Hz";
    }
}

TEST_F(GameLoopTest, SlowCallbackReportsOverrun)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 1000.0;  // 1ms per tick
    wow::GameLoop loop(config);

    // Callback that takes ~10ms — guaranteed to overrun a 1ms tick.
    loop.on_tick([](uint64_t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    loop.stop();

    auto metrics = filter_entries("metric", "game_loop");
    ASSERT_GT(metrics.size(), 0u);

    bool any_overrun = false;
    for (const auto& m : metrics) {
        if (m.contains("data") && m["data"].value("overrun", false)) {
            any_overrun = true;
            break;
        }
    }
    EXPECT_TRUE(any_overrun)
        << "Slow callback should cause at least one overrun at 1000 Hz";
}

// ===========================================================================
// Group F: Timing Sanity
// ===========================================================================

TEST_F(GameLoopTest, TickRateApproximatelyCorrect)
{
    init_logger();
    wow::GameLoopConfig config;
    config.tick_rate_hz = 100.0;  // 10ms per tick
    wow::GameLoop loop(config);

    loop.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop.stop();

    // At 100 Hz for 200ms, expect ~20 ticks. Use generous bounds
    // (10–30) for CI scheduler variance.
    auto ticks = loop.tick_count();
    EXPECT_GE(ticks, 10u) << "Too few ticks — loop is too slow";
    EXPECT_LE(ticks, 30u) << "Too many ticks — loop is too fast";
}
