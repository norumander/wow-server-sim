#include "server/fault/scenarios.h"

namespace wow {

// --- F1: LatencySpikeFault ---

FaultId LatencySpikeFault::id() const { return "latency-spike"; }
std::string LatencySpikeFault::description() const { return "Add configurable delay to tick processing"; }
FaultMode LatencySpikeFault::mode() const { return FaultMode::TICK_SCOPED; }
bool LatencySpikeFault::activate(const FaultConfig& /*config*/) { return false; }
void LatencySpikeFault::deactivate() {}
bool LatencySpikeFault::is_active() const { return false; }
void LatencySpikeFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {}
FaultStatus LatencySpikeFault::status() const { return FaultStatus{id(), mode()}; }

// --- F2: SessionCrashFault ---

FaultId SessionCrashFault::id() const { return "session-crash"; }
std::string SessionCrashFault::description() const { return "Force-terminate a random player session"; }
FaultMode SessionCrashFault::mode() const { return FaultMode::TICK_SCOPED; }
bool SessionCrashFault::activate(const FaultConfig& /*config*/) { return false; }
void SessionCrashFault::deactivate() {}
bool SessionCrashFault::is_active() const { return false; }
void SessionCrashFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {}
FaultStatus SessionCrashFault::status() const { return FaultStatus{id(), mode()}; }

// --- F3: EventQueueFloodFault ---

FaultId EventQueueFloodFault::id() const { return "event-queue-flood"; }
std::string EventQueueFloodFault::description() const { return "Inject multiplied synthetic events into zone queue"; }
FaultMode EventQueueFloodFault::mode() const { return FaultMode::TICK_SCOPED; }
bool EventQueueFloodFault::activate(const FaultConfig& /*config*/) { return false; }
void EventQueueFloodFault::deactivate() {}
bool EventQueueFloodFault::is_active() const { return false; }
void EventQueueFloodFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {}
FaultStatus EventQueueFloodFault::status() const { return FaultStatus{id(), mode()}; }

// --- F4: MemoryPressureFault ---

FaultId MemoryPressureFault::id() const { return "memory-pressure"; }
std::string MemoryPressureFault::description() const { return "Allocate and hold large memory buffers"; }
FaultMode MemoryPressureFault::mode() const { return FaultMode::AMBIENT; }
bool MemoryPressureFault::activate(const FaultConfig& /*config*/) { return false; }
void MemoryPressureFault::deactivate() {}
bool MemoryPressureFault::is_active() const { return false; }
void MemoryPressureFault::on_tick(uint64_t /*current_tick*/, Zone* /*zone*/) {}
FaultStatus MemoryPressureFault::status() const { return FaultStatus{id(), mode()}; }
size_t MemoryPressureFault::bytes_allocated() const { return 0; }

}  // namespace wow
