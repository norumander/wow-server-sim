#include "server/fault/scenarios.h"

#include <chrono>
#include <cstring>
#include <stdexcept>
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

// --- F5: CascadingZoneFailureFault ---

FaultId CascadingZoneFailureFault::id() const { return "cascading-zone-failure"; }
std::string CascadingZoneFailureFault::description() const { return "Crash source zone, flood target zone with events"; }
FaultMode CascadingZoneFailureFault::mode() const { return FaultMode::TICK_SCOPED; }

bool CascadingZoneFailureFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    fired_crash_ = false;
    source_crashed_ = false;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("source_zone")) {
        source_zone_ = config.params["source_zone"].get<uint32_t>();
    } else {
        source_zone_ = 1;
    }
    if (config.params.contains("target_zone")) {
        target_zone_ = config.params["target_zone"].get<uint32_t>();
    } else {
        target_zone_ = 2;
    }
    if (config.params.contains("flood_multiplier")) {
        flood_multiplier_ = config.params["flood_multiplier"].get<uint32_t>();
    } else {
        flood_multiplier_ = 10;
    }
    return true;
}

void CascadingZoneFailureFault::deactivate() {
    active_ = false;
    fired_crash_ = false;
    source_crashed_ = false;
}

bool CascadingZoneFailureFault::is_active() const { return active_; }

void CascadingZoneFailureFault::on_tick(uint64_t current_tick, Zone* zone) {
    if (!active_ || zone == nullptr) {
        return;
    }

    // Phase 1: Crash the source zone
    if (zone->zone_id() == source_zone_ && !fired_crash_) {
        fired_crash_ = true;
        source_crashed_ = true;

        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Cascading failure: crashing source zone", {
                {"fault_id", id()},
                {"source_zone", source_zone_},
                {"target_zone", target_zone_}
            });
        }

        throw std::runtime_error("Cascading zone failure: source zone crash injected");
    }

    // Phase 2: Flood the target zone after source has crashed
    if (zone->zone_id() == target_zone_ && source_crashed_) {
        const auto& entities = zone->entities();
        size_t total_events = entities.size() * flood_multiplier_;

        size_t i = 0;
        for (const auto& [session_id, entity] : entities) {
            for (uint32_t m = 0; m < flood_multiplier_; ++m) {
                float x = static_cast<float>((current_tick * 31 + i * 7 + session_id) % 1000);
                float y = static_cast<float>((current_tick * 13 + i * 11 + session_id) % 1000);
                zone->push_event(std::make_unique<MovementEvent>(
                    session_id, Position{x, y, 0.0f}));
                ++i;
            }
        }

        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Cascading failure: target zone flooded", {
                {"fault_id", id()},
                {"target_zone", target_zone_},
                {"events_injected", total_events}
            });
        }
    }
}

FaultStatus CascadingZoneFailureFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F6: SlowLeakFault ---

FaultId SlowLeakFault::id() const { return "slow-leak"; }
std::string SlowLeakFault::description() const { return "Increment tick processing delay over time"; }
FaultMode SlowLeakFault::mode() const { return FaultMode::TICK_SCOPED; }

bool SlowLeakFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    current_delay_ms_ = 0;
    tick_counter_ = 0;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("increment_ms")) {
        increment_ms_ = config.params["increment_ms"].get<uint32_t>();
    } else {
        increment_ms_ = 1;
    }
    if (config.params.contains("increment_every")) {
        increment_every_ = config.params["increment_every"].get<uint32_t>();
    } else {
        increment_every_ = 100;
    }
    return true;
}

void SlowLeakFault::deactivate() {
    active_ = false;
    current_delay_ms_ = 0;
    tick_counter_ = 0;
}

bool SlowLeakFault::is_active() const { return active_; }

void SlowLeakFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    if (!active_) {
        return;
    }
    ++tick_counter_;
    if (increment_every_ > 0 && tick_counter_ % increment_every_ == 0) {
        current_delay_ms_ += increment_ms_;
    }
    if (current_delay_ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms_));
    }
}

uint32_t SlowLeakFault::current_delay_ms() const { return current_delay_ms_; }

FaultStatus SlowLeakFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F7: SplitBrainFault ---

FaultId SplitBrainFault::id() const { return "split-brain"; }
std::string SplitBrainFault::description() const { return "Create phantom entities with divergent state across zones"; }
FaultMode SplitBrainFault::mode() const { return FaultMode::TICK_SCOPED; }

bool SplitBrainFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    phantoms_created_.clear();
    tick_counter_ = 0;
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("phantom_count")) {
        phantom_count_ = config.params["phantom_count"].get<uint32_t>();
    } else {
        phantom_count_ = 2;
    }
    if (config.params.contains("phantom_base_id")) {
        phantom_base_id_ = config.params["phantom_base_id"].get<uint64_t>();
    } else {
        phantom_base_id_ = 2000001;
    }
    return true;
}

void SplitBrainFault::deactivate() {
    active_ = false;
    phantoms_created_.clear();
    tick_counter_ = 0;
}

bool SplitBrainFault::is_active() const { return active_; }

void SplitBrainFault::on_tick(uint64_t /*current_tick*/, Zone* zone) {
    if (!active_ || zone == nullptr) {
        return;
    }

    ++tick_counter_;
    auto zid = zone->zone_id();

    // Phase 1: Create phantom entities on first tick per zone
    if (!phantoms_created_[zid]) {
        for (uint32_t i = 0; i < phantom_count_; ++i) {
            uint64_t phantom_id = phantom_base_id_ + i;
            zone->add_entity(Entity(phantom_id, EntityType::NPC));
        }
        phantoms_created_[zid] = true;

        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Split brain: phantoms created", {
                {"fault_id", id()},
                {"zone_id", zid},
                {"phantom_count", phantom_count_}
            });
        }
    }

    // Phase 2: Inject divergent movement events every tick
    for (uint32_t i = 0; i < phantom_count_; ++i) {
        uint64_t phantom_id = phantom_base_id_ + i;
        Position pos;
        if (zid % 2 == 1) {
            // Odd zone: move east (along x)
            pos = {static_cast<float>(tick_counter_ * 10), 0.0f, 0.0f};
        } else {
            // Even zone: move north (along y)
            pos = {0.0f, static_cast<float>(tick_counter_ * 10), 0.0f};
        }
        zone->push_event(std::make_unique<MovementEvent>(phantom_id, pos));
    }

    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Split brain: divergent state", {
            {"fault_id", id()},
            {"zone_id", zid},
            {"tick_counter", tick_counter_}
        });
    }
}

FaultStatus SplitBrainFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

// --- F8: ThunderingHerdFault ---

FaultId ThunderingHerdFault::id() const { return "thundering-herd"; }
std::string ThunderingHerdFault::description() const { return "Mass disconnect all players, then simultaneous reconnect"; }
FaultMode ThunderingHerdFault::mode() const { return FaultMode::TICK_SCOPED; }

bool ThunderingHerdFault::activate(const FaultConfig& config) {
    config_ = config;
    active_ = true;
    disconnect_done_.clear();
    stored_players_.clear();
    disconnect_tick_ = 0;
    reconnect_done_.clear();
    ticks_elapsed_ = 0;
    ++activations_;
    if (config.params.contains("reconnect_delay_ticks")) {
        reconnect_delay_ticks_ = config.params["reconnect_delay_ticks"].get<uint32_t>();
    } else {
        reconnect_delay_ticks_ = 20;
    }
    return true;
}

void ThunderingHerdFault::deactivate() {
    active_ = false;
    disconnect_done_.clear();
    stored_players_.clear();
    disconnect_tick_ = 0;
    reconnect_done_.clear();
}

bool ThunderingHerdFault::is_active() const { return active_; }

void ThunderingHerdFault::on_tick(uint64_t current_tick, Zone* zone) {
    if (!active_ || zone == nullptr) {
        return;
    }

    auto zid = zone->zone_id();

    // Phase 1: Mass disconnect — remove all PLAYER entities, store IDs
    if (!disconnect_done_[zid]) {
        disconnect_done_[zid] = true;
        if (disconnect_tick_ == 0) {
            disconnect_tick_ = current_tick;
        }

        std::vector<uint64_t> player_ids;
        for (const auto& [session_id, entity] : zone->entities()) {
            if (entity.entity_type() == EntityType::PLAYER) {
                player_ids.push_back(session_id);
            }
        }

        for (auto pid : player_ids) {
            zone->remove_entity(pid);
        }
        stored_players_[zid] = std::move(player_ids);

        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Thundering herd: mass disconnect", {
                {"fault_id", id()},
                {"zone_id", zid},
                {"players_disconnected", stored_players_[zid].size()}
            });
        }
        return;
    }

    // Phase 2: Mass reconnect after delay
    if (!reconnect_done_[zid] && disconnect_tick_ > 0 &&
        current_tick >= disconnect_tick_ + reconnect_delay_ticks_) {
        reconnect_done_[zid] = true;

        for (auto pid : stored_players_[zid]) {
            zone->add_entity(Entity(pid, EntityType::PLAYER));
        }

        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Thundering herd: mass reconnect", {
                {"fault_id", id()},
                {"zone_id", zid},
                {"players_reconnected", stored_players_[zid].size()}
            });
        }
    }
}

FaultStatus ThunderingHerdFault::status() const {
    FaultStatus s;
    s.id = id();
    s.mode = mode();
    s.active = active_;
    s.activations = activations_;
    s.ticks_elapsed = ticks_elapsed_;
    s.config = active_ ? config_.params : nlohmann::json::object();
    return s;
}

}  // namespace wow
