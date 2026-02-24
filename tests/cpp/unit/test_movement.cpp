#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "server/events/event.h"
#include "server/events/event_queue.h"
#include "server/events/movement.h"
#include "server/telemetry/logger.h"
#include "server/world/entity.h"

using json = nlohmann::json;

// ===========================================================================
// Test fixture — initializes Logger with ostringstream sink for telemetry
// tests. Data-type-only tests (Groups A–D) skip Logger initialization.
// ===========================================================================

class MovementTest : public ::testing::Test {
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

    /// Filter log entries by message substring.
    std::vector<json> filter_by_message(const std::string& substr)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            auto msg = entry.value("message", "");
            if (msg.find(substr) != std::string::npos) {
                result.push_back(entry);
            }
        }
        return result;
    }
};

// ===========================================================================
// Group A: Position (3 tests)
// ===========================================================================

TEST_F(MovementTest, Position_DefaultIsOrigin)
{
    wow::Position pos;
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);
}

TEST_F(MovementTest, Position_EqualityForIdenticalPositions)
{
    wow::Position a{1.0f, 2.0f, 3.0f};
    wow::Position b{1.0f, 2.0f, 3.0f};
    wow::Position c{4.0f, 5.0f, 6.0f};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST_F(MovementTest, Position_DistanceBetweenTwoPoints)
{
    // 3-4-5 triangle in XY plane: distance should be 5.0
    wow::Position a{0.0f, 0.0f, 0.0f};
    wow::Position b{3.0f, 4.0f, 0.0f};

    EXPECT_FLOAT_EQ(wow::distance(a, b), 5.0f);

    // Distance to self is 0.
    EXPECT_FLOAT_EQ(wow::distance(a, a), 0.0f);

    // 3D distance: sqrt(1^2 + 2^2 + 2^2) = sqrt(9) = 3.0
    wow::Position c{1.0f, 2.0f, 2.0f};
    EXPECT_FLOAT_EQ(wow::distance(a, c), 3.0f);
}

// ===========================================================================
// Group B: GameEvent Base (3 tests)
// ===========================================================================

TEST_F(MovementTest, GameEvent_StoresTypeAndSessionId)
{
    wow::MovementEvent evt(42, wow::Position{1.0f, 2.0f, 3.0f});

    EXPECT_EQ(evt.event_type(), wow::EventType::MOVEMENT);
    EXPECT_EQ(evt.session_id(), 42u);
}

TEST_F(MovementTest, GameEvent_EventTypeToStringConvertsAll)
{
    EXPECT_EQ(wow::event_type_to_string(wow::EventType::MOVEMENT), "MOVEMENT");
    EXPECT_EQ(wow::event_type_to_string(wow::EventType::SPELL_CAST), "SPELL_CAST");
    EXPECT_EQ(wow::event_type_to_string(wow::EventType::COMBAT), "COMBAT");
}

TEST_F(MovementTest, GameEvent_PolymorphicDestructionViaBasePointer)
{
    // Ensure unique_ptr<GameEvent> correctly destroys a MovementEvent.
    // This test verifies the virtual destructor works — if it doesn't,
    // this will trigger AddressSanitizer or Valgrind reports.
    auto evt = std::make_unique<wow::MovementEvent>(1, wow::Position{10.0f, 20.0f, 30.0f});
    std::unique_ptr<wow::GameEvent> base = std::move(evt);

    EXPECT_EQ(base->event_type(), wow::EventType::MOVEMENT);
    EXPECT_EQ(base->session_id(), 1u);
    // Destruction happens here — no crash = pass.
}

// ===========================================================================
// Group C: MovementEvent (2 tests)
// ===========================================================================

TEST_F(MovementTest, MovementEvent_HasMovementType)
{
    wow::MovementEvent evt(1, wow::Position{});
    EXPECT_EQ(evt.event_type(), wow::EventType::MOVEMENT);
}

TEST_F(MovementTest, MovementEvent_StoresTargetPosition)
{
    wow::Position target{100.0f, -50.0f, 25.0f};
    wow::MovementEvent evt(7, target);

    EXPECT_EQ(evt.position(), target);
}

// ===========================================================================
// Group D: Entity (3 tests)
// ===========================================================================

TEST_F(MovementTest, Entity_DefaultPositionIsOrigin)
{
    wow::Entity entity(1);
    wow::Position origin{0.0f, 0.0f, 0.0f};
    EXPECT_EQ(entity.position(), origin);
}

TEST_F(MovementTest, Entity_StoresSessionId)
{
    wow::Entity entity(42);
    EXPECT_EQ(entity.session_id(), 42u);
}

TEST_F(MovementTest, Entity_SetPositionUpdatesPosition)
{
    wow::Entity entity(1);
    wow::Position new_pos{10.0f, 20.0f, 30.0f};
    entity.set_position(new_pos);
    EXPECT_EQ(entity.position(), new_pos);
}

// ===========================================================================
// Group E: EventQueue (4 tests)
// ===========================================================================

TEST_F(MovementTest, EventQueue_DrainReturnsEmptyWhenEmpty)
{
    wow::EventQueue queue;
    auto events = queue.drain();
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
}

TEST_F(MovementTest, EventQueue_PushAndDrainRoundTrips)
{
    wow::EventQueue queue;

    queue.push(std::make_unique<wow::MovementEvent>(1, wow::Position{1.0f, 0.0f, 0.0f}));
    queue.push(std::make_unique<wow::MovementEvent>(2, wow::Position{2.0f, 0.0f, 0.0f}));
    queue.push(std::make_unique<wow::MovementEvent>(3, wow::Position{3.0f, 0.0f, 0.0f}));

    EXPECT_EQ(queue.size(), 3u);
    EXPECT_FALSE(queue.empty());

    auto events = queue.drain();
    ASSERT_EQ(events.size(), 3u);

    // Verify events are returned in push order.
    EXPECT_EQ(events[0]->session_id(), 1u);
    EXPECT_EQ(events[1]->session_id(), 2u);
    EXPECT_EQ(events[2]->session_id(), 3u);
}

TEST_F(MovementTest, EventQueue_DrainClearsQueue)
{
    wow::EventQueue queue;
    queue.push(std::make_unique<wow::MovementEvent>(1, wow::Position{}));

    auto first = queue.drain();
    EXPECT_EQ(first.size(), 1u);

    auto second = queue.drain();
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(MovementTest, EventQueue_ConcurrentPushAndDrainDoNotCorrupt)
{
    wow::EventQueue queue;
    const int kEventsPerThread = 100;
    const int kNumThreads = 4;

    std::atomic<int> total_drained{0};

    // Spawn producer threads.
    std::vector<std::thread> producers;
    for (int t = 0; t < kNumThreads; ++t) {
        producers.emplace_back([&queue, t, kEventsPerThread]() {
            for (int i = 0; i < kEventsPerThread; ++i) {
                auto session_id = static_cast<uint64_t>(t * kEventsPerThread + i);
                queue.push(std::make_unique<wow::MovementEvent>(
                    session_id, wow::Position{static_cast<float>(i), 0.0f, 0.0f}));
            }
        });
    }

    // Spawn consumer thread that drains repeatedly.
    std::thread consumer([&queue, &total_drained]() {
        for (int round = 0; round < 200; ++round) {
            auto batch = queue.drain();
            total_drained.fetch_add(static_cast<int>(batch.size()));
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    // Drain any remaining events after producers finished.
    auto remaining = queue.drain();
    total_drained.fetch_add(static_cast<int>(remaining.size()));

    EXPECT_EQ(total_drained.load(), kNumThreads * kEventsPerThread);
}

// ===========================================================================
// Group F: MovementProcessor (5 tests)
// ===========================================================================

TEST_F(MovementTest, MovementProcessor_UpdatesEntityPosition)
{
    init_logger();
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{10.0f, 20.0f, 30.0f}));

    size_t updated = processor.process(events, entities);

    EXPECT_EQ(updated, 1u);
    EXPECT_EQ(entities.at(1).position(), (wow::Position{10.0f, 20.0f, 30.0f}));
}

TEST_F(MovementTest, MovementProcessor_SkipsUnknownSessionId)
{
    init_logger();
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    // No entity with session_id 99.

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(99, wow::Position{1.0f, 2.0f, 3.0f}));

    size_t updated = processor.process(events, entities);

    EXPECT_EQ(updated, 0u);
    EXPECT_EQ(entities.count(99), 0u);

    // Should emit a warning.
    auto warnings = filter_by_message("Unknown session");
    EXPECT_GE(warnings.size(), 1u);
}

TEST_F(MovementTest, MovementProcessor_ProcessesMultipleEntities)
{
    init_logger();
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{10.0f, 0.0f, 0.0f}));
    events.push_back(std::make_unique<wow::MovementEvent>(2, wow::Position{20.0f, 0.0f, 0.0f}));

    size_t updated = processor.process(events, entities);

    EXPECT_EQ(updated, 2u);
    EXPECT_EQ(entities.at(1).position(), (wow::Position{10.0f, 0.0f, 0.0f}));
    EXPECT_EQ(entities.at(2).position(), (wow::Position{20.0f, 0.0f, 0.0f}));
}

TEST_F(MovementTest, MovementProcessor_LastEventWinsForSameSession)
{
    init_logger();
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{10.0f, 0.0f, 0.0f}));
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{20.0f, 0.0f, 0.0f}));
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{30.0f, 0.0f, 0.0f}));

    size_t updated = processor.process(events, entities);

    // Only 1 entity was updated (same session, multiple events).
    EXPECT_EQ(updated, 1u);
    // Last position wins.
    EXPECT_EQ(entities.at(1).position(), (wow::Position{30.0f, 0.0f, 0.0f}));
}

TEST_F(MovementTest, MovementProcessor_EmitsTelemetryPerMovement)
{
    init_logger();
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(1, wow::Position{5.0f, 0.0f, 0.0f}));
    events.push_back(std::make_unique<wow::MovementEvent>(2, wow::Position{10.0f, 0.0f, 0.0f}));

    processor.process(events, entities);

    auto telemetry = filter_by_message("Position updated");
    EXPECT_EQ(telemetry.size(), 2u);

    // Verify telemetry contains expected data fields.
    for (const auto& entry : telemetry) {
        EXPECT_TRUE(entry.contains("data"));
        const auto& data = entry["data"];
        EXPECT_TRUE(data.contains("session_id"));
        EXPECT_TRUE(data.contains("new_x"));
        EXPECT_TRUE(data.contains("new_y"));
        EXPECT_TRUE(data.contains("new_z"));
    }
}

// ===========================================================================
// Group G: Tick Integration (1 test)
// ===========================================================================

TEST_F(MovementTest, TickIntegration_QueuedEventsProcessedViaCallback)
{
    init_logger();

    wow::EventQueue queue;
    wow::MovementProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});

    // Simulate network thread pushing events.
    queue.push(std::make_unique<wow::MovementEvent>(1, wow::Position{100.0f, 200.0f, 0.0f}));
    queue.push(std::make_unique<wow::MovementEvent>(2, wow::Position{-50.0f, 75.0f, 10.0f}));

    // Simulate game tick: drain queue, then process.
    auto events = queue.drain();
    ASSERT_EQ(events.size(), 2u);

    size_t updated = processor.process(events, entities);
    EXPECT_EQ(updated, 2u);

    // Verify positions.
    EXPECT_EQ(entities.at(1).position(), (wow::Position{100.0f, 200.0f, 0.0f}));
    EXPECT_EQ(entities.at(2).position(), (wow::Position{-50.0f, 75.0f, 10.0f}));

    // Queue should be empty after drain.
    EXPECT_TRUE(queue.empty());
}
