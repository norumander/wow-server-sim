#include "server/fault/scenarios.h"

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
    // Stub — on_tick behavior implemented in Commit 4
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

void SessionCrashFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    // Stub — on_tick behavior implemented in Commit 4
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

void EventQueueFloodFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    // Stub — on_tick behavior implemented in Commit 4
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
    // Stub — actual allocation implemented in Commit 4
    return true;
}

void MemoryPressureFault::deactivate() {
    active_ = false;
    // Stub — actual deallocation implemented in Commit 4
}

bool MemoryPressureFault::is_active() const { return active_; }

void MemoryPressureFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {
    // Stub — on_tick behavior implemented in Commit 4
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

size_t MemoryPressureFault::bytes_allocated() const { return 0; }

}  // namespace wow
