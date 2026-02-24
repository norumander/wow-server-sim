#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/events/event.h"
#include "server/events/movement.h"
#include "server/events/spellcast.h"
#include "server/telemetry/logger.h"
#include "server/world/entity.h"

using json = nlohmann::json;

// ===========================================================================
// Test fixture — mirrors MovementTest pattern with telemetry sink
// ===========================================================================

class SpellCastTest : public ::testing::Test {
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

    /// Helper: create an entity map with one entity for the given session.
    std::unordered_map<uint64_t, wow::Entity> make_entities(uint64_t session_id)
    {
        std::unordered_map<uint64_t, wow::Entity> entities;
        entities.emplace(session_id, wow::Entity{session_id});
        return entities;
    }

    /// Helper: create a CAST_START event.
    std::unique_ptr<wow::GameEvent> make_cast_start(uint64_t session_id,
                                                     uint32_t spell_id,
                                                     uint32_t cast_time_ticks)
    {
        return std::make_unique<wow::SpellCastEvent>(
            session_id, wow::SpellAction::CAST_START, spell_id, cast_time_ticks);
    }

    /// Helper: create an INTERRUPT event.
    std::unique_ptr<wow::GameEvent> make_interrupt(uint64_t session_id)
    {
        return std::make_unique<wow::SpellCastEvent>(
            session_id, wow::SpellAction::INTERRUPT, 0, 0);
    }
};

// ===========================================================================
// Group A: SpellCastEvent Data (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, SpellCastEvent_HasSpellCastType)
{
    wow::SpellCastEvent evt(1, wow::SpellAction::CAST_START, 100, 10);
    EXPECT_EQ(evt.event_type(), wow::EventType::SPELL_CAST);
}

TEST_F(SpellCastTest, SpellCastEvent_StoresCastStartFields)
{
    wow::SpellCastEvent evt(42, wow::SpellAction::CAST_START, 200, 60);

    EXPECT_EQ(evt.session_id(), 42u);
    EXPECT_EQ(evt.action(), wow::SpellAction::CAST_START);
    EXPECT_EQ(evt.spell_id(), 200u);
    EXPECT_EQ(evt.cast_time_ticks(), 60u);
}

TEST_F(SpellCastTest, SpellCastEvent_StoresInterruptAction)
{
    wow::SpellCastEvent evt(7, wow::SpellAction::INTERRUPT, 0, 0);

    EXPECT_EQ(evt.action(), wow::SpellAction::INTERRUPT);
    EXPECT_EQ(evt.session_id(), 7u);
}

// ===========================================================================
// Group B: CastState and Entity (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, Entity_CastStateDefaultNotCasting)
{
    wow::Entity entity(1);
    const auto& cs = entity.cast_state();

    EXPECT_FALSE(cs.is_casting);
    EXPECT_EQ(cs.spell_id, 0u);
    EXPECT_EQ(cs.cast_ticks_remaining, 0u);
    EXPECT_EQ(cs.gcd_expires_tick, 0u);
}

TEST_F(SpellCastTest, Entity_CastStateMutableAccess)
{
    wow::Entity entity(1);

    // Write via mutable accessor.
    entity.cast_state().is_casting = true;
    entity.cast_state().spell_id = 42;
    entity.cast_state().cast_ticks_remaining = 10;
    entity.cast_state().gcd_expires_tick = 100;

    // Read back via const accessor.
    const wow::Entity& cref = entity;
    EXPECT_TRUE(cref.cast_state().is_casting);
    EXPECT_EQ(cref.cast_state().spell_id, 42u);
    EXPECT_EQ(cref.cast_state().cast_ticks_remaining, 10u);
    EXPECT_EQ(cref.cast_state().gcd_expires_tick, 100u);
}

TEST_F(SpellCastTest, CastState_MovedThisTickDefaultFalse)
{
    wow::Entity entity(1);
    EXPECT_FALSE(entity.cast_state().moved_this_tick);
}

// ===========================================================================
// Group C: Cast Initiation (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, Processor_CastStartSetsCastingState)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    processor.process(events, entities, 0);

    const auto& cs = entities.at(1).cast_state();
    EXPECT_TRUE(cs.is_casting);
    EXPECT_EQ(cs.spell_id, 100u);
    EXPECT_EQ(cs.cast_ticks_remaining, 20u);
}

TEST_F(SpellCastTest, Processor_CastStartReturnsOneStarted)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    auto result = processor.process(events, entities, 0);
    EXPECT_EQ(result.casts_started, 1u);
}

TEST_F(SpellCastTest, Processor_CastStartOnUnknownSessionSkips)
{
    init_logger();
    wow::SpellCastProcessor processor;
    std::unordered_map<uint64_t, wow::Entity> entities;  // empty

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(99, 100, 20));

    auto result = processor.process(events, entities, 0);
    EXPECT_EQ(result.casts_started, 0u);

    // Should warn about unknown session.
    auto warnings = filter_by_message("Unknown session");
    EXPECT_GE(warnings.size(), 1u);
}

// ===========================================================================
// Group D: GCD Enforcement (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, Processor_GcdBlocksNewCast)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Manually set GCD that hasn't expired yet.
    entities.at(1).cast_state().gcd_expires_tick = 50;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    // Process at tick 10 — GCD expires at 50, so blocked.
    auto result = processor.process(events, entities, 10);
    EXPECT_EQ(result.gcd_blocked, 1u);
    EXPECT_EQ(result.casts_started, 0u);
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);
}

TEST_F(SpellCastTest, Processor_GcdExpiryAllowsNewCast)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // GCD expires exactly at current tick — allowed.
    entities.at(1).cast_state().gcd_expires_tick = 50;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    auto result = processor.process(events, entities, 50);
    EXPECT_EQ(result.casts_started, 1u);
    EXPECT_EQ(result.gcd_blocked, 0u);
    EXPECT_TRUE(entities.at(1).cast_state().is_casting);
}

TEST_F(SpellCastTest, Processor_CastStartSetsGcd)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    processor.process(events, entities, 10);

    // GCD should be set to current_tick + kGlobalCooldownTicks.
    EXPECT_EQ(entities.at(1).cast_state().gcd_expires_tick,
              10u + wow::kGlobalCooldownTicks);
}

// ===========================================================================
// Group E: Cast Advancement & Completion (4 tests)
// ===========================================================================

TEST_F(SpellCastTest, Processor_CastTimerDecrementsEachTick)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Manually set up an active cast with 5 ticks remaining.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 5;

    // Process with no events — just timer advancement.
    std::vector<std::unique_ptr<wow::GameEvent>> events;
    processor.process(events, entities, 100);

    EXPECT_EQ(entities.at(1).cast_state().cast_ticks_remaining, 4u);
    EXPECT_TRUE(entities.at(1).cast_state().is_casting);
}

TEST_F(SpellCastTest, Processor_CastCompletesWhenTimerReachesZero)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Cast with 1 tick remaining — should complete this tick.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 1;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    auto result = processor.process(events, entities, 100);

    EXPECT_EQ(result.casts_completed, 1u);
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);
}

TEST_F(SpellCastTest, Processor_CompletedCastClearsSpellId)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 1;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    processor.process(events, entities, 100);

    EXPECT_EQ(entities.at(1).cast_state().spell_id, 0u);
}

TEST_F(SpellCastTest, Processor_InstantCastCompletesImmediately)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // cast_time_ticks = 0 means instant cast.
    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 200, 0));

    auto result = processor.process(events, entities, 10);

    // Both started and completed in the same tick.
    EXPECT_EQ(result.casts_started, 1u);
    EXPECT_EQ(result.casts_completed, 1u);

    // Should NOT remain in casting state.
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);

    // GCD should still be set.
    EXPECT_EQ(entities.at(1).cast_state().gcd_expires_tick,
              10u + wow::kGlobalCooldownTicks);
}

// ===========================================================================
// Group F: Interrupt Handling (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, Processor_InterruptEventCancelsActiveCast)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Set up active cast.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 10;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_interrupt(1));

    auto result = processor.process(events, entities, 50);

    EXPECT_EQ(result.casts_interrupted, 1u);
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);
    EXPECT_EQ(entities.at(1).cast_state().spell_id, 0u);
}

TEST_F(SpellCastTest, Processor_InterruptOnNonCastingEntityIsNoop)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Entity is NOT casting.
    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_interrupt(1));

    auto result = processor.process(events, entities, 50);

    EXPECT_EQ(result.casts_interrupted, 0u);
}

TEST_F(SpellCastTest, Processor_MovementCancelsCast)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Set up active cast + movement flag.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 10;
    cs.moved_this_tick = true;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    auto result = processor.process(events, entities, 50);

    EXPECT_EQ(result.casts_interrupted, 1u);
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);
    // moved_this_tick should be cleared after processing.
    EXPECT_FALSE(entities.at(1).cast_state().moved_this_tick);
}

// ===========================================================================
// Group G: Telemetry (3 tests)
// ===========================================================================

TEST_F(SpellCastTest, Processor_EmitsTelemetryOnCastStart)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_cast_start(1, 100, 20));

    processor.process(events, entities, 0);

    auto entries = filter_by_message("Cast started");
    ASSERT_GE(entries.size(), 1u);

    const auto& data = entries[0]["data"];
    EXPECT_EQ(data["session_id"], 1u);
    EXPECT_EQ(data["spell_id"], 100u);
}

TEST_F(SpellCastTest, Processor_EmitsTelemetryOnCastComplete)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Set up cast that completes this tick.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 1;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    processor.process(events, entities, 50);

    auto entries = filter_by_message("Cast completed");
    ASSERT_GE(entries.size(), 1u);

    const auto& data = entries[0]["data"];
    EXPECT_EQ(data["session_id"], 1u);
    EXPECT_EQ(data["spell_id"], 100u);
}

TEST_F(SpellCastTest, Processor_EmitsTelemetryOnCastInterrupted)
{
    init_logger();
    wow::SpellCastProcessor processor;
    auto entities = make_entities(1);

    // Set up active cast + interrupt event.
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 10;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_interrupt(1));

    processor.process(events, entities, 50);

    auto entries = filter_by_message("Cast interrupted");
    ASSERT_GE(entries.size(), 1u);

    const auto& data = entries[0]["data"];
    EXPECT_EQ(data["session_id"], 1u);
    EXPECT_TRUE(data.contains("reason"));
}

// ===========================================================================
// Group H: Integration (2 tests)
// ===========================================================================

TEST_F(SpellCastTest, TickIntegration_MovementThenSpellCastCancelsCast)
{
    init_logger();

    // Set up entity with an active cast.
    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    auto& cs = entities.at(1).cast_state();
    cs.is_casting = true;
    cs.spell_id = 100;
    cs.cast_ticks_remaining = 10;

    // Phase 1: Movement processor updates position AND sets moved_this_tick.
    wow::MovementProcessor move_proc;
    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(std::make_unique<wow::MovementEvent>(
        1, wow::Position{10.0f, 20.0f, 0.0f}));

    move_proc.process(events, entities);

    // Verify movement set the flag.
    EXPECT_TRUE(entities.at(1).cast_state().moved_this_tick);

    // Phase 2: SpellCast processor should cancel the cast due to movement.
    wow::SpellCastProcessor spell_proc;
    std::vector<std::unique_ptr<wow::GameEvent>> spell_events;

    auto result = spell_proc.process(spell_events, entities, 50);

    EXPECT_EQ(result.casts_interrupted, 1u);
    EXPECT_FALSE(entities.at(1).cast_state().is_casting);
    EXPECT_FALSE(entities.at(1).cast_state().moved_this_tick);
}

TEST_F(SpellCastTest, TickIntegration_FullCastLifecycle)
{
    init_logger();
    auto entities = make_entities(1);
    wow::SpellCastProcessor processor;
    const uint64_t start_tick = 100;
    const uint32_t cast_time = 3;  // 3 ticks to complete

    // Tick 100: Start cast.
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        events.push_back(make_cast_start(1, 500, cast_time));
        auto result = processor.process(events, entities, start_tick);
        EXPECT_EQ(result.casts_started, 1u);
        EXPECT_TRUE(entities.at(1).cast_state().is_casting);
        EXPECT_EQ(entities.at(1).cast_state().cast_ticks_remaining, cast_time);
    }

    // Tick 101: Advance timer (3 → 2).
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        auto result = processor.process(events, entities, start_tick + 1);
        EXPECT_EQ(result.casts_completed, 0u);
        EXPECT_EQ(entities.at(1).cast_state().cast_ticks_remaining, 2u);
    }

    // Tick 102: Advance timer (2 → 1).
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        auto result = processor.process(events, entities, start_tick + 2);
        EXPECT_EQ(result.casts_completed, 0u);
        EXPECT_EQ(entities.at(1).cast_state().cast_ticks_remaining, 1u);
    }

    // Tick 103: Cast completes (1 → 0).
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        auto result = processor.process(events, entities, start_tick + 3);
        EXPECT_EQ(result.casts_completed, 1u);
        EXPECT_FALSE(entities.at(1).cast_state().is_casting);
        EXPECT_EQ(entities.at(1).cast_state().spell_id, 0u);
    }

    // Verify telemetry trail.
    auto started = filter_by_message("Cast started");
    auto completed = filter_by_message("Cast completed");
    EXPECT_EQ(started.size(), 1u);
    EXPECT_EQ(completed.size(), 1u);
}
