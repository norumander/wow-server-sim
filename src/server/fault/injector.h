#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace wow {

class Zone;

/// Unique identifier for a fault scenario (e.g. "latency-spike").
using FaultId = std::string;

/// Distinguishes faults that fire during zone tick hooks from ambient faults.
enum class FaultMode {
    TICK_SCOPED,  ///< Fires via execute_pre_tick_faults() inside zone tick
    AMBIENT,      ///< Runs independently when activated (e.g. memory pressure)
};

/// Configuration passed to Fault::activate().
struct FaultConfig {
    nlohmann::json params;          ///< Fault-specific parameters (delay_ms, megabytes, etc.)
    uint32_t target_zone_id = 0;    ///< Zone to target (0 = all zones)
    uint64_t duration_ticks = 0;    ///< Auto-deactivate after N ticks (0 = indefinite)
};

/// Snapshot of a fault's current status, for telemetry and monitoring.
struct FaultStatus {
    FaultId id;
    FaultMode mode = FaultMode::TICK_SCOPED;
    bool active = false;
    uint64_t activations = 0;       ///< Total number of times activated
    uint64_t ticks_elapsed = 0;     ///< Ticks since last activation
    nlohmann::json config;          ///< Active config (empty object if inactive)
};

/// Abstract base class for all fault injection scenarios.
///
/// Concrete faults implement on_tick() to inject their failure behavior.
/// Tick-scoped faults receive a Zone pointer; ambient faults receive nullptr.
class Fault {
public:
    virtual ~Fault() = default;

    /// Unique identifier (e.g. "latency-spike", "session-crash").
    virtual FaultId id() const = 0;

    /// Human-readable description of the fault scenario.
    virtual std::string description() const = 0;

    /// Whether this fault fires per-zone (TICK_SCOPED) or globally (AMBIENT).
    virtual FaultMode mode() const = 0;

    /// Activate the fault with the given configuration.
    virtual bool activate(const FaultConfig& config) = 0;

    /// Deactivate the fault, releasing any resources.
    virtual void deactivate() = 0;

    /// Whether the fault is currently active.
    virtual bool is_active() const = 0;

    /// Called each tick. Zone is non-null for tick-scoped faults, null for ambient.
    virtual void on_tick(uint64_t current_tick, Zone* zone) = 0;

    /// Snapshot of current status for telemetry.
    virtual FaultStatus status() const = 0;

protected:
    Fault() = default;
};

/// Owns and manages all registered fault scenarios.
///
/// Not a singleton — created and owned by the game server for testability.
/// Provides zone-hook wiring via execute_pre_tick_faults() and ambient
/// fault ticking via on_tick().
class FaultRegistry {
public:
    FaultRegistry() = default;

    /// Register a fault scenario. Returns false if ID already registered.
    bool register_fault(std::unique_ptr<Fault> fault);

    /// Activate a fault by ID. Returns false if ID not found.
    bool activate(const FaultId& id, const FaultConfig& config = {});

    /// Deactivate a fault by ID. Returns false if ID not found.
    bool deactivate(const FaultId& id);

    /// Deactivate all active faults.
    void deactivate_all();

    /// Check if a fault is currently active.
    bool is_active(const FaultId& id) const;

    /// Get the status of a specific fault.
    std::optional<FaultStatus> fault_status(const FaultId& id) const;

    /// Get the status of all registered faults.
    std::vector<FaultStatus> all_status() const;

    /// Get all registered fault IDs.
    std::vector<FaultId> registered_ids() const;

    /// Number of registered faults.
    size_t fault_count() const;

    /// Number of currently active faults.
    size_t active_count() const;

    /// Tick ambient faults and track duration for auto-deactivation.
    /// Called once per game tick, before zone_manager.tick_all().
    void on_tick(uint64_t current_tick);

    /// Execute all active tick-scoped faults for a specific zone.
    /// Called from zone pre_tick_hook, inside the exception guard.
    void execute_pre_tick_faults(Zone& zone);

private:
    /// Per-fault activation tracking for duration auto-deactivation.
    struct ActivationInfo {
        FaultConfig config;
        uint64_t ticks_elapsed = 0;
    };

    std::unordered_map<FaultId, std::unique_ptr<Fault>> faults_;
    std::unordered_map<FaultId, ActivationInfo> activations_;
    uint64_t current_tick_ = 0;
};

}  // namespace wow
