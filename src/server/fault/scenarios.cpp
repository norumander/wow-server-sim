#include "server/fault/scenarios.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "server/events/movement.h"
#include "server/telemetry/logger.h"
#include "server/world/zone.h"

namespace wow {

// --- F1: LatencySpikeFault ---

FaultId LatencySpikeFault::id() const { return "latency-spike"; }
std::string LatencySpikeFault::description() const { return "Add configurable delay to tick processing"; }
FaultMode LatencySpikeFault::mode() const { return FaultMode::TICK_SCOPED; }

bool LatencySpikeFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("delay_ms")) {
        delay_ms_ = config.params["delay_ms"].get<uint32_t>();
    } else {
        delay_ms_ = 200;
    }
    return true;
}

void LatencySpikeFault::deactivate() {
    active_ = false;
}

bool LatencySpikeFault::is_active() const { return active_; }

void LatencySpikeFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    if (!active_) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
}

FaultStatus LatencySpikeFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F2: SessionCrashFault ---

FaultId SessionCrashFault::id() const { return "session-crash"; }
std::string SessionCrashFault::description() const { return "Force-terminate a random player session"; }
FaultMode SessionCrashFault::mode() const { return FaultMode::TICK_SCOPED; }

bool SessionCrashFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    fired_ = false;
    ticks_elapsed_ = 0;
    ++activations_;
    return true;
}

void SessionCrashFault::deactivate() {
    active_ = false;
    fired_ = false;
}

bool SessionCrashFault::is_active() const { return active_; }

void SessionCrashFault::on_tick(uint64_t /*current_tick*/, Zone* zone) {
    if (!active_ || fired_ || zone == nullptr) {
        return;
    }
    const auto& entities = zone->entities();
    if (entities.empty()) {
        return;
    }
    // Remove the first entity (unordered_map iteration order is stable per instance)
    auto victim_id = entities.begin()->first;
    zone->remove_entity(victim_id);
    fired_ = true;

    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Session crashed by fault injection", {
            {"fault_id", id()},
            {"session_id", victim_id},
            {"zone_id", zone->zone_id()}
        });
    }
}

FaultStatus SessionCrashFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F3: EventQueueFloodFault ---

FaultId EventQueueFloodFault::id() const { return "event-queue-flood"; }
std::string EventQueueFloodFault::description() const { return "Inject multiplied synthetic events into zone queue"; }
FaultMode EventQueueFloodFault::mode() const { return FaultMode::TICK_SCOPED; }

bool EventQueueFloodFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("multiplier")) {
        multiplier_ = config.params["multiplier"].get<uint32_t>();
    } else {
        multiplier_ = 10;
    }
    return true;
}

void EventQueueFloodFault::deactivate() {
    active_ = false;
}

bool EventQueueFloodFault::is_active() const { return active_; }

void EventQueueFloodFault::on_tick(uint64_t current_tick, Zone* zone) {
    if (!active_ || zone == nullptr) {
        return;
    }
    const auto& entities = zone->entities();
    size_t total_events = entities.size() * multiplier_;

    size_t i = 0;
    for (const auto& [session_id, entity] : entities) {
        for (uint32_t m = 0; m < multiplier_; ++m) {
            // Deterministic position: no <random> header, MSVC-safe
            float x = static_cast<float>((current_tick * 31 + i * 7 + session_id) % 1000);
            float y = static_cast<float>((current_tick * 13 + i * 11 + session_id) % 1000);
            float z = 0.0f;
            zone->push_event(std::make_unique<MovementEvent>(
                session_id, Position{x, y, z}));
            ++i;
        }
    }

    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Event queue flooded", {
            {"fault_id", id()},
            {"zone_id", zone->zone_id()},
            {"events_injected", total_events}
        });
    }
}

FaultStatus EventQueueFloodFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F4: MemoryPressureFault ---

FaultId MemoryPressureFault::id() const { return "memory-pressure"; }
std::string MemoryPressureFault::description() const { return "Allocate and hold large memory buffers"; }
FaultMode MemoryPressureFault::mode() const { return FaultMode::AMBIENT; }

bool MemoryPressureFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("megabytes")) {
        megabytes_ = config.params["megabytes"].get<uint32_t>();
    } else {
        megabytes_ = 64;
    }

    // Allocate in 1MB chunks, filled with 0xAB to ensure OS commits pages
    constexpr size_t kOneMB = 1024 * 1024;
    buffers_.clear();
    buffers_.reserve(megabytes_);
    for (uint32_t i = 0; i < megabytes_; ++i) {
        buffers_.emplace_back(kOneMB, static_cast<uint8_t>(0xAB));
    }

    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Memory pressure applied", {
            {"fault_id", id()},
            {"megabytes", megabytes_},
            {"bytes_allocated", bytes_allocated()}
        });
    }
    return true;
}

void MemoryPressureFault::deactivate() {
    active_ = false;
    buffers_.clear();
    buffers_.shrink_to_fit();

    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Memory pressure released", {
            {"fault_id", id()}
        });
    }
}

bool MemoryPressureFault::is_active() const { return active_; }

void MemoryPressureFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    // Ambient fault — buffers held while active, nothing to do per tick
}

FaultStatus MemoryPressureFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

size_t MemoryPressureFault::bytes_allocated() const {
    size_t total = 0;
    for (const auto& buf : buffers_) {
        total += buf.size();
    }
    return total;
}

}  // namespace wow
