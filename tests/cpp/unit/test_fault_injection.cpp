#include <gtest/gtest.h>

#include <chrono>
#include <sstream>
#include <string>

#include "server/fault/injector.h"
#include "server/fault/scenarios.h"
#include "server/telemetry/logger.h"
#include "server/world/zone.h"

using namespace wow;

// =============================================================================
// Group A: FaultRegistry Registration (3 tests)
// =============================================================================

TEST(FaultRegistry, ConstructionDefaults) {
    FaultRegistry registry;
    EXPECT_EQ(registry.fault_count(), 0u);
    EXPECT_EQ(registry.active_count(), 0u);
    EXPECT_TRUE(registry.registered_ids().empty());
}

TEST(FaultRegistry, RegisterFaultSucceeds) {
    FaultRegistry registry;
    auto fault = std::make_unique<LatencySpikeFault>();
    EXPECT_TRUE(registry.register_fault(std::move(fault)));
    EXPECT_EQ(registry.fault_count(), 1u);
    auto ids = registry.registered_ids();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "latency-spike");
}

TEST(FaultRegistry, RegisterDuplicateReturnsFalse) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    auto duplicate = std::make_unique<LatencySpikeFault>();
    EXPECT_FALSE(registry.register_fault(std::move(duplicate)));
    EXPECT_EQ(registry.fault_count(), 1u);
}

// =============================================================================
// Group B: FaultRegistry Activation/Deactivation (4 tests)
// =============================================================================

TEST(FaultRegistry, ActivateSucceeds) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    EXPECT_TRUE(registry.activate("latency-spike"));
    EXPECT_TRUE(registry.is_active("latency-spike"));
    EXPECT_EQ(registry.active_count(), 1u);
}

TEST(FaultRegistry, ActivateUnknownReturnsFalse) {
    FaultRegistry registry;
    EXPECT_FALSE(registry.activate("nonexistent-fault"));
}

TEST(FaultRegistry, DeactivateSucceeds) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    registry.activate("latency-spike");
    EXPECT_TRUE(registry.deactivate("latency-spike"));
    EXPECT_FALSE(registry.is_active("latency-spike"));
    EXPECT_EQ(registry.active_count(), 0u);
}

TEST(FaultRegistry, DeactivateAllClearsActive) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    registry.register_fault(std::make_unique<MemoryPressureFault>());
    registry.activate("latency-spike");
    registry.activate("memory-pressure");
    EXPECT_EQ(registry.active_count(), 2u);
    registry.deactivate_all();
    EXPECT_EQ(registry.active_count(), 0u);
}

// =============================================================================
// Group C: FaultRegistry Duration, Status, Telemetry (4 tests)
// =============================================================================

TEST(FaultRegistry, FaultStatusReflectsActivation) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());

    FaultConfig config;
    config.params = {{"delay_ms", 100}};
    registry.activate("latency-spike", config);

    auto status = registry.fault_status("latency-spike");
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->id, "latency-spike");
    EXPECT_TRUE(status->active);
    EXPECT_EQ(status->mode, FaultMode::TICK_SCOPED);
}

TEST(FaultRegistry, AllStatusReturnsAllRegistered) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    registry.register_fault(std::make_unique<MemoryPressureFault>());
    registry.activate("latency-spike");

    auto all = registry.all_status();
    EXPECT_EQ(all.size(), 2u);
}

TEST(FaultRegistry, DurationAutoDeactivates) {
    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());

    FaultConfig config;
    config.duration_ticks = 5;
    registry.activate("latency-spike", config);
    EXPECT_TRUE(registry.is_active("latency-spike"));

    // Tick 5 times — fault should auto-deactivate
    for (uint64_t t = 1; t <= 5; ++t) {
        registry.on_tick(t);
    }
    EXPECT_FALSE(registry.is_active("latency-spike"));
}

TEST(FaultRegistry, ActivateAndDeactivateEmitTelemetry) {
    // Set up logger with custom sink to capture output
    std::ostringstream log_output;
    LoggerConfig log_config;
    log_config.custom_sink = &log_output;
    Logger::initialize(log_config);

    FaultRegistry registry;
    registry.register_fault(std::make_unique<LatencySpikeFault>());
    registry.activate("latency-spike");
    registry.deactivate("latency-spike");

    std::string output = log_output.str();
    EXPECT_NE(output.find("Fault activated"), std::string::npos);
    EXPECT_NE(output.find("Fault deactivated"), std::string::npos);

    Logger::reset();
}

// =============================================================================
// Group D: LatencySpikeFault F1 (3 tests)
// =============================================================================

TEST(LatencySpikeFault, IdAndModeCorrect) {
    LatencySpikeFault fault;
    EXPECT_EQ(fault.id(), "latency-spike");
    EXPECT_EQ(fault.mode(), FaultMode::TICK_SCOPED);
}

TEST(LatencySpikeFault, ActivateDeactivateLifecycle) {
    LatencySpikeFault fault;
    EXPECT_FALSE(fault.is_active());

    FaultConfig config;
    EXPECT_TRUE(fault.activate(config));
    EXPECT_TRUE(fault.is_active());

    fault.deactivate();
    EXPECT_FALSE(fault.is_active());
}

TEST(LatencySpikeFault, OnTickIntroducesDelay) {
    LatencySpikeFault fault;
    FaultConfig config;
    config.params = {{"delay_ms", 50}};
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});

    auto start = std::chrono::steady_clock::now();
    fault.on_tick(1, &zone);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    EXPECT_GE(elapsed_ms, 50.0);
}

// =============================================================================
// Group E: SessionCrashFault F2 (4 tests)
// =============================================================================

TEST(SessionCrashFault, IdAndModeCorrect) {
    SessionCrashFault fault;
    EXPECT_EQ(fault.id(), "session-crash");
    EXPECT_EQ(fault.mode(), FaultMode::TICK_SCOPED);
}

TEST(SessionCrashFault, RemovesEntityFromZone) {
    SessionCrashFault fault;
    FaultConfig config;
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});
    zone.add_entity(Entity(100));
    zone.add_entity(Entity(101));
    zone.add_entity(Entity(102));
    ASSERT_EQ(zone.entity_count(), 3u);

    fault.on_tick(1, &zone);
    EXPECT_EQ(zone.entity_count(), 2u);
}

TEST(SessionCrashFault, FiresOncePerActivation) {
    SessionCrashFault fault;
    FaultConfig config;
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});
    zone.add_entity(Entity(100));
    zone.add_entity(Entity(101));
    zone.add_entity(Entity(102));

    fault.on_tick(1, &zone);
    EXPECT_EQ(zone.entity_count(), 2u);

    // Second on_tick should NOT remove another entity
    fault.on_tick(2, &zone);
    EXPECT_EQ(zone.entity_count(), 2u);
}

TEST(SessionCrashFault, EmptyZoneDoesNotCrash) {
    SessionCrashFault fault;
    FaultConfig config;
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});
    ASSERT_EQ(zone.entity_count(), 0u);

    // Should not crash or throw
    fault.on_tick(1, &zone);
    EXPECT_EQ(zone.entity_count(), 0u);
}

// =============================================================================
// Group F: EventQueueFloodFault F3 (3 tests)
// =============================================================================

TEST(EventQueueFloodFault, IdAndModeCorrect) {
    EventQueueFloodFault fault;
    EXPECT_EQ(fault.id(), "event-queue-flood");
    EXPECT_EQ(fault.mode(), FaultMode::TICK_SCOPED);
}

TEST(EventQueueFloodFault, InjectsEventsPerEntity) {
    EventQueueFloodFault fault;
    FaultConfig config;
    config.params = {{"multiplier", 10}};
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});
    zone.add_entity(Entity(100));
    zone.add_entity(Entity(101));

    fault.on_tick(1, &zone);

    // 2 entities * 10 multiplier = 20 events
    EXPECT_GE(zone.event_queue_depth(), 20u);
}

TEST(EventQueueFloodFault, CustomMultiplierFromConfig) {
    EventQueueFloodFault fault;
    FaultConfig config;
    config.params = {{"multiplier", 5}};
    fault.activate(config);

    Zone zone(ZoneConfig{1, "Test Zone"});
    zone.add_entity(Entity(100));
    zone.add_entity(Entity(101));

    fault.on_tick(1, &zone);

    // 2 entities * 5 multiplier = 10 events
    EXPECT_GE(zone.event_queue_depth(), 10u);
}

// =============================================================================
// Group G: MemoryPressureFault F4 (3 tests)
// =============================================================================

TEST(MemoryPressureFault, IdAndModeCorrect) {
    MemoryPressureFault fault;
    EXPECT_EQ(fault.id(), "memory-pressure");
    EXPECT_EQ(fault.mode(), FaultMode::AMBIENT);
}

TEST(MemoryPressureFault, AllocatesOnActivation) {
    MemoryPressureFault fault;
    FaultConfig config;
    config.params = {{"megabytes", 1}};
    fault.activate(config);

    EXPECT_GE(fault.bytes_allocated(), 1u * 1024u * 1024u);
}

TEST(MemoryPressureFault, ReleasesOnDeactivation) {
    MemoryPressureFault fault;
    FaultConfig config;
    config.params = {{"megabytes", 1}};
    fault.activate(config);
    ASSERT_GE(fault.bytes_allocated(), 1u * 1024u * 1024u);

    fault.deactivate();
    EXPECT_EQ(fault.bytes_allocated(), 0u);
}
