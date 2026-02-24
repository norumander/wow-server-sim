#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/fault/injector.h"

namespace wow {

/// F1: Latency Spike — adds configurable delay to zone tick processing.
///
/// Simulates network or processing latency by sleeping during on_tick().
/// Default delay: 200ms. Configurable via params["delay_ms"].
class LatencySpikeFault : public Fault {
public:
    FaultId id() const override;
    std::string description() const override;
    FaultMode mode() const override;
    bool activate(const FaultConfig& config) override;
    void deactivate() override;
    bool is_active() const override;
    void on_tick(uint64_t current_tick, Zone* zone) override;
    FaultStatus status() const override;

private:
    bool active_ = false;
    FaultConfig config_;
    uint64_t activations_ = 0;
    uint64_t ticks_elapsed_ = 0;
    uint32_t delay_ms_ = 200;
};

/// F2: Session Crash — force-terminates a player session in a zone.
///
/// Removes the first entity from the zone. Fires once per activation
/// (re-activation resets the fired flag). Simulates unexpected disconnects.
class SessionCrashFault : public Fault {
public:
    FaultId id() const override;
    std::string description() const override;
    FaultMode mode() const override;
    bool activate(const FaultConfig& config) override;
    void deactivate() override;
    bool is_active() const override;
    void on_tick(uint64_t current_tick, Zone* zone) override;
    FaultStatus status() const override;

private:
    bool active_ = false;
    bool fired_ = false;
    FaultConfig config_;
    uint64_t activations_ = 0;
    uint64_t ticks_elapsed_ = 0;
};

/// F3: Event Queue Flood — injects multiplier * entity_count synthetic events.
///
/// Pushes MovementEvents with deterministic positions into the zone's queue.
/// Default multiplier: 10. Configurable via params["multiplier"].
class EventQueueFloodFault : public Fault {
public:
    FaultId id() const override;
    std::string description() const override;
    FaultMode mode() const override;
    bool activate(const FaultConfig& config) override;
    void deactivate() override;
    bool is_active() const override;
    void on_tick(uint64_t current_tick, Zone* zone) override;
    FaultStatus status() const override;

private:
    bool active_ = false;
    FaultConfig config_;
    uint64_t activations_ = 0;
    uint64_t ticks_elapsed_ = 0;
    uint32_t multiplier_ = 10;
};

/// F4: Memory Pressure — allocates and holds large memory buffers.
///
/// Allocates megabytes MB in 1MB chunks on activation, releases on deactivation.
/// Buffers filled with 0xAB to ensure OS commits the pages.
/// Default: 64 MB. Configurable via params["megabytes"].
class MemoryPressureFault : public Fault {
public:
    FaultId id() const override;
    std::string description() const override;
    FaultMode mode() const override;
    bool activate(const FaultConfig& config) override;
    void deactivate() override;
    bool is_active() const override;
    void on_tick(uint64_t current_tick, Zone* zone) override;
    FaultStatus status() const override;

    /// Number of bytes currently allocated by this fault.
    size_t bytes_allocated() const;

private:
    bool active_ = false;
    FaultConfig config_;
    uint64_t activations_ = 0;
    uint64_t ticks_elapsed_ = 0;
    uint32_t megabytes_ = 64;
    std::vector<std::vector<uint8_t>> buffers_;
};

}  // namespace wow
