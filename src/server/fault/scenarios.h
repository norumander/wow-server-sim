#pragma once

#include <cstdint>
#include <map>
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

/// F5: Cascading Zone Failure — crashes source zone, then floods target zone.
///
/// Multi-phase fault: throws std::runtime_error in the source zone (crashing it
/// via the zone exception guard), then injects flood_multiplier * entity_count
/// synthetic MovementEvents in the target zone on subsequent ticks.
class CascadingZoneFailureFault : public Fault {
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
    uint32_t source_zone_ = 1;
    uint32_t target_zone_ = 2;
    uint32_t flood_multiplier_ = 10;
    bool fired_crash_ = false;
    bool source_crashed_ = false;
};

/// F6: Slow Leak — increments tick processing delay over time.
///
/// Each tick increments a counter. Every `increment_every` ticks, adds
/// `increment_ms` to the accumulated delay and sleeps for the total.
/// Simulates gradual performance degradation (e.g., memory leak, cache bloat).
class SlowLeakFault : public Fault {
public:
    FaultId id() const override;
    std::string description() const override;
    FaultMode mode() const override;
    bool activate(const FaultConfig& config) override;
    void deactivate() override;
    bool is_active() const override;
    void on_tick(uint64_t current_tick, Zone* zone) override;
    FaultStatus status() const override;

    /// Current accumulated delay in milliseconds (for status/testing).
    uint32_t current_delay_ms() const;

private:
    bool active_ = false;
    FaultConfig config_;
    uint64_t activations_ = 0;
    uint64_t ticks_elapsed_ = 0;
    uint32_t increment_ms_ = 1;
    uint32_t increment_every_ = 100;
    uint32_t current_delay_ms_ = 0;
    uint64_t tick_counter_ = 0;
};

/// F7: Split Brain — creates phantom entities with divergent state across zones.
///
/// On first tick per zone, adds phantom_count entities. On every tick, injects
/// MovementEvents with zone-dependent positions: odd zone_id moves east,
/// even zone_id moves north. Same entity ID in multiple zones with different
/// positions = state divergence (the "split brain" signal).
class SplitBrainFault : public Fault {
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
    uint32_t phantom_count_ = 2;
    uint64_t phantom_base_id_ = 2000001;
    std::map<uint32_t, bool> phantoms_created_;
    uint64_t tick_counter_ = 0;
};

}  // namespace wow
