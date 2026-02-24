#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <stdexcept>

#include "server/events/combat.h"
#include "server/events/movement.h"
#include "server/events/spellcast.h"
#include "server/telemetry/logger.h"
#include "server/world/zone.h"
#include "server/world/zone_manager.h"

using namespace wow;

// ---------------------------------------------------------------------------
// Helper: Logger setup/teardown for tests that verify telemetry
// ---------------------------------------------------------------------------
class ZoneTestWithLogger : public ::testing::Test {
protected:
    std::ostringstream log_output_;

    void SetUp() override {
        if (Logger::is_initialized()) Logger::reset();
        LoggerConfig config;
        config.custom_sink = &log_output_;
        Logger::initialize(config);
    }

    void TearDown() override {
        if (Logger::is_initialized()) Logger::reset();
    }
};

// ===========================================================================
// Group A: Zone Construction (2 tests)
// ===========================================================================

TEST(Zone, ConstructionStoresConfig) {
    ZoneConfig config{1, "Elwynn Forest"};
    Zone zone(config);

    EXPECT_EQ(zone.zone_id(), 1u);
    EXPECT_EQ(zone.name(), "Elwynn Forest");
    EXPECT_EQ(zone.state(), ZoneState::ACTIVE);
    EXPECT_EQ(zone.entity_count(), 0u);
}

TEST(Zone, InitialHealthDefaults) {
    ZoneConfig config{2, "Westfall"};
    Zone zone(config);

    auto h = zone.health();
    EXPECT_EQ(h.zone_id, 2u);
    EXPECT_EQ(h.state, ZoneState::ACTIVE);
    EXPECT_EQ(h.total_ticks, 0u);
    EXPECT_EQ(h.error_count, 0u);
    EXPECT_EQ(h.entity_count, 0u);
    EXPECT_EQ(h.event_queue_depth, 0u);
    EXPECT_DOUBLE_EQ(h.last_tick_duration_ms, 0.0);
}

// ===========================================================================
// Group B: Zone Entity Management (4 tests)
// ===========================================================================

TEST(Zone, AddEntitySucceeds) {
    Zone zone(ZoneConfig{1, "Test"});
    Entity player(100);

    EXPECT_TRUE(zone.add_entity(std::move(player)));
    EXPECT_EQ(zone.entity_count(), 1u);
    EXPECT_TRUE(zone.has_entity(100));
}

TEST(Zone, AddDuplicateEntityReturnsFalse) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.add_entity(Entity(100));

    EXPECT_FALSE(zone.add_entity(Entity(100)));
    EXPECT_EQ(zone.entity_count(), 1u);
}

TEST(Zone, RemoveEntitySucceeds) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.add_entity(Entity(100));

    EXPECT_TRUE(zone.remove_entity(100));
    EXPECT_EQ(zone.entity_count(), 0u);
    EXPECT_FALSE(zone.has_entity(100));
}

TEST(Zone, TakeEntityReturnsAndRemoves) {
    Zone zone(ZoneConfig{1, "Test"});
    Entity player(100);
    player.set_position({10.0f, 20.0f, 30.0f});
    player.combat_state().health = 50;
    zone.add_entity(std::move(player));

    auto taken = zone.take_entity(100);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->session_id(), 100u);
    EXPECT_EQ(taken->position().x, 10.0f);
    EXPECT_EQ(taken->position().y, 20.0f);
    EXPECT_EQ(taken->position().z, 30.0f);
    EXPECT_EQ(taken->combat_state().health, 50);
    EXPECT_FALSE(zone.has_entity(100));
    EXPECT_EQ(zone.entity_count(), 0u);
}

// ===========================================================================
// Group C: Zone Event Delivery (2 tests)
// ===========================================================================

TEST(Zone, PushEventIncreasesQueueDepth) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.push_event(std::make_unique<MovementEvent>(100, Position{1, 2, 3}));
    EXPECT_GT(zone.event_queue_depth(), 0u);
}

TEST(Zone, EventsDrainedOnTick) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.add_entity(Entity(100));
    zone.push_event(std::make_unique<MovementEvent>(100, Position{1, 2, 3}));
    zone.push_event(std::make_unique<MovementEvent>(100, Position{4, 5, 6}));

    auto result = zone.tick(1);
    EXPECT_EQ(result.events_processed, 2u);
    EXPECT_EQ(zone.event_queue_depth(), 0u);
}

// ===========================================================================
// Group D: Zone Tick Pipeline (4 tests)
// ===========================================================================

TEST(Zone, TickProcessesMovementEvents) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.add_entity(Entity(100));
    zone.push_event(std::make_unique<MovementEvent>(100, Position{5, 10, 15}));

    zone.tick(1);

    auto& entity = zone.entities().at(100);
    EXPECT_EQ(entity.position().x, 5.0f);
    EXPECT_EQ(entity.position().y, 10.0f);
    EXPECT_EQ(entity.position().z, 15.0f);
}

TEST(Zone, TickProcessesSpellCastEvents) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.add_entity(Entity(100));
    // Spell 42 with 10-tick cast time
    zone.push_event(std::make_unique<SpellCastEvent>(
        100, SpellAction::CAST_START, 42, 10));

    auto result = zone.tick(1);

    auto& entity = zone.entities().at(100);
    EXPECT_TRUE(entity.cast_state().is_casting);
    EXPECT_EQ(entity.cast_state().spell_id, 42u);
    EXPECT_EQ(result.spell_result.casts_started, 1u);
}

TEST(Zone, TickProcessesCombatEvents) {
    Zone zone(ZoneConfig{1, "Test"});

    Entity attacker(1);
    Entity target(2);
    target.combat_state().health = 100;
    target.combat_state().max_health = 100;
    target.combat_state().armor = 0.0f;
    zone.add_entity(std::move(attacker));
    zone.add_entity(std::move(target));

    zone.push_event(std::make_unique<CombatEvent>(
        1, CombatAction::ATTACK, 2, 30, DamageType::PHYSICAL));

    auto result = zone.tick(1);

    EXPECT_EQ(result.combat_result.attacks_processed, 1u);
    EXPECT_EQ(zone.entities().at(2).combat_state().health, 70);
}

TEST(Zone, TickFullPipeline_MovementCancelsCast) {
    Zone zone(ZoneConfig{1, "Test"});
    Entity player(100);
    // Start with an active cast
    player.cast_state().is_casting = true;
    player.cast_state().spell_id = 99;
    player.cast_state().cast_ticks_remaining = 5;
    zone.add_entity(std::move(player));

    // Movement in same tick should cancel the cast
    zone.push_event(std::make_unique<MovementEvent>(100, Position{1, 2, 3}));

    auto result = zone.tick(1);

    auto& entity = zone.entities().at(100);
    EXPECT_FALSE(entity.cast_state().is_casting);
    EXPECT_EQ(result.spell_result.casts_interrupted, 1u);
}

// ===========================================================================
// Group E: Zone Exception Guard (4 tests)
// ===========================================================================

TEST(Zone, ExceptionGuardCatchesStdException) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.set_pre_tick_hook([]() {
        throw std::runtime_error("test fault");
    });

    auto result = zone.tick(1);
    EXPECT_TRUE(result.had_error);
    EXPECT_NE(result.error_message.find("test fault"), std::string::npos);
}

TEST(Zone, ExceptionGuardCatchesUnknownException) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.set_pre_tick_hook([]() {
        throw 42;  // non-std::exception
    });

    auto result = zone.tick(1);
    EXPECT_TRUE(result.had_error);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(Zone, ExceptionGuardSetsStateToCrashed) {
    Zone zone(ZoneConfig{1, "Test"});
    zone.set_pre_tick_hook([]() {
        throw std::runtime_error("crash");
    });

    zone.tick(1);
    EXPECT_EQ(zone.state(), ZoneState::CRASHED);
}

TEST(Zone, StateRecoversToDegradedThenActive) {
    Zone zone(ZoneConfig{1, "Test"});

    // Crash the zone
    zone.set_pre_tick_hook([]() {
        throw std::runtime_error("crash");
    });
    zone.tick(1);
    EXPECT_EQ(zone.state(), ZoneState::CRASHED);

    // Remove the hook — next tick should succeed
    zone.set_pre_tick_hook(nullptr);
    zone.tick(2);
    EXPECT_EQ(zone.state(), ZoneState::DEGRADED);

    // Another successful tick → ACTIVE
    zone.tick(3);
    EXPECT_EQ(zone.state(), ZoneState::ACTIVE);
}

// ===========================================================================
// Group F: Zone Health & Telemetry (2 tests)
// ===========================================================================

TEST_F(ZoneTestWithLogger, TickEmitsTelemetryMetric) {
    Zone zone(ZoneConfig{1, "Elwynn"});
    zone.add_entity(Entity(100));
    zone.tick(1);

    std::string output = log_output_.str();
    EXPECT_NE(output.find("Zone tick completed"), std::string::npos);
    EXPECT_NE(output.find("\"zone_id\":1"), std::string::npos);
}

TEST_F(ZoneTestWithLogger, ExceptionEmitsTelemetryError) {
    Zone zone(ZoneConfig{1, "Elwynn"});
    zone.set_pre_tick_hook([]() {
        throw std::runtime_error("injected fault");
    });
    zone.tick(1);

    std::string output = log_output_.str();
    EXPECT_NE(output.find("Zone tick exception"), std::string::npos);
    EXPECT_NE(output.find("error"), std::string::npos);
}

// ===========================================================================
// Group G: ZoneManager Zone Lifecycle (2 tests)
// ===========================================================================

TEST(ZoneManager, CreateZoneAndGetZone) {
    ZoneManager mgr;
    auto id = mgr.create_zone(ZoneConfig{1, "Elwynn Forest"});

    EXPECT_EQ(id, 1u);
    EXPECT_EQ(mgr.zone_count(), 1u);

    auto* zone = mgr.get_zone(1);
    ASSERT_NE(zone, nullptr);
    EXPECT_EQ(zone->zone_id(), 1u);
    EXPECT_EQ(zone->name(), "Elwynn Forest");
}

TEST(ZoneManager, GetNonexistentZoneReturnsNull) {
    ZoneManager mgr;
    EXPECT_EQ(mgr.get_zone(999), nullptr);
}

// ===========================================================================
// Group H: ZoneManager Session Assignment (3 tests)
// ===========================================================================

TEST(ZoneManager, AssignSessionCreatesEntity) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Test"});

    EXPECT_TRUE(mgr.assign_session(100, 1));
    EXPECT_EQ(mgr.session_zone(100), 1u);

    auto* zone = mgr.get_zone(1);
    ASSERT_NE(zone, nullptr);
    EXPECT_TRUE(zone->has_entity(100));
    EXPECT_EQ(zone->entity_count(), 1u);
}

TEST(ZoneManager, AssignToNonexistentZoneFails) {
    ZoneManager mgr;
    EXPECT_FALSE(mgr.assign_session(100, 999));
    EXPECT_EQ(mgr.session_zone(100), kNoZone);
}

TEST(ZoneManager, RemoveSessionSucceeds) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Test"});
    mgr.assign_session(100, 1);

    EXPECT_TRUE(mgr.remove_session(100));
    EXPECT_EQ(mgr.session_zone(100), kNoZone);

    auto* zone = mgr.get_zone(1);
    EXPECT_FALSE(zone->has_entity(100));
    EXPECT_EQ(zone->entity_count(), 0u);
}

// ===========================================================================
// Group I: ZoneManager Session Transfer (2 tests)
// ===========================================================================

TEST(ZoneManager, TransferMovesEntityBetweenZones) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Source"});
    mgr.create_zone(ZoneConfig{2, "Target"});
    mgr.assign_session(100, 1);

    EXPECT_TRUE(mgr.transfer_session(100, 2));
    EXPECT_EQ(mgr.session_zone(100), 2u);
    EXPECT_FALSE(mgr.get_zone(1)->has_entity(100));
    EXPECT_TRUE(mgr.get_zone(2)->has_entity(100));
}

TEST(ZoneManager, TransferPreservesEntityState) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Source"});
    mgr.create_zone(ZoneConfig{2, "Target"});
    mgr.assign_session(100, 1);

    // Modify entity state in source zone
    auto* source = mgr.get_zone(1);
    source->push_event(std::make_unique<MovementEvent>(100, Position{10, 20, 30}));
    source->tick(1);

    // Transfer to target zone
    mgr.transfer_session(100, 2);

    // Verify state was preserved
    auto& entity = mgr.get_zone(2)->entities().at(100);
    EXPECT_EQ(entity.position().x, 10.0f);
    EXPECT_EQ(entity.position().y, 20.0f);
    EXPECT_EQ(entity.position().z, 30.0f);
}

// ===========================================================================
// Group J: ZoneManager Event Routing (3 tests)
// ===========================================================================

TEST(ZoneManager, RouteEventsToCorrectZones) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Zone1"});
    mgr.create_zone(ZoneConfig{2, "Zone2"});
    mgr.assign_session(100, 1);
    mgr.assign_session(200, 2);

    std::vector<std::unique_ptr<GameEvent>> events;
    events.push_back(std::make_unique<MovementEvent>(100, Position{1, 0, 0}));
    events.push_back(std::make_unique<MovementEvent>(200, Position{2, 0, 0}));

    auto routed = mgr.route_events(events);
    EXPECT_EQ(routed, 2u);
    EXPECT_EQ(mgr.get_zone(1)->event_queue_depth(), 1u);
    EXPECT_EQ(mgr.get_zone(2)->event_queue_depth(), 1u);
}

TEST(ZoneManager, RouteUnknownSessionDiscards) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Zone1"});
    mgr.assign_session(100, 1);

    std::vector<std::unique_ptr<GameEvent>> events;
    events.push_back(std::make_unique<MovementEvent>(100, Position{1, 0, 0}));
    events.push_back(std::make_unique<MovementEvent>(999, Position{2, 0, 0}));

    auto routed = mgr.route_events(events);
    EXPECT_EQ(routed, 1u);
    EXPECT_EQ(mgr.get_zone(1)->event_queue_depth(), 1u);
}

TEST(ZoneManager, RoutedEventsProcessedOnTick) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Zone1"});
    mgr.assign_session(100, 1);

    std::vector<std::unique_ptr<GameEvent>> events;
    events.push_back(std::make_unique<MovementEvent>(100, Position{5, 10, 15}));
    mgr.route_events(events);

    auto result = mgr.tick_all(1);

    auto& entity = mgr.get_zone(1)->entities().at(100);
    EXPECT_EQ(entity.position().x, 5.0f);
    EXPECT_EQ(entity.position().y, 10.0f);
    EXPECT_EQ(entity.position().z, 15.0f);
    EXPECT_EQ(result.total_events, 1u);
}

// ===========================================================================
// Group K: ZoneManager Tick All & Isolation (2 tests)
// ===========================================================================

TEST(ZoneManager, TickAllProcessesAllZones) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "Zone1"});
    mgr.create_zone(ZoneConfig{2, "Zone2"});

    auto result = mgr.tick_all(1);
    EXPECT_EQ(result.zones_ticked, 2u);
    EXPECT_EQ(result.zone_results.size(), 2u);
}

TEST(ZoneManager, CrashedZoneDoesNotAffectOthers) {
    ZoneManager mgr;
    mgr.create_zone(ZoneConfig{1, "CrashZone"});
    mgr.create_zone(ZoneConfig{2, "HealthyZone"});
    mgr.assign_session(200, 2);

    // Inject fault into zone 1
    mgr.get_zone(1)->set_pre_tick_hook([]() {
        throw std::runtime_error("zone 1 crash");
    });

    // Push a movement event for zone 2
    std::vector<std::unique_ptr<GameEvent>> events;
    events.push_back(std::make_unique<MovementEvent>(200, Position{7, 8, 9}));
    mgr.route_events(events);

    auto result = mgr.tick_all(1);

    // Zone 1 crashed but zone 2 processed normally
    EXPECT_EQ(result.zones_with_errors, 1u);
    EXPECT_EQ(mgr.get_zone(1)->state(), ZoneState::CRASHED);
    EXPECT_EQ(mgr.get_zone(2)->state(), ZoneState::ACTIVE);

    auto& entity = mgr.get_zone(2)->entities().at(200);
    EXPECT_EQ(entity.position().x, 7.0f);
    EXPECT_EQ(entity.position().y, 8.0f);
    EXPECT_EQ(entity.position().z, 9.0f);
}
