#include <gtest/gtest.h>

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
