#include "server/fault/injector.h"

#include "server/telemetry/logger.h"
#include "server/world/zone.h"

namespace wow {

bool FaultRegistry::register_fault(std::unique_ptr<Fault> fault) {
    if (!fault) {
        return false;
    }
    auto id = fault->id();
    auto [it, inserted] = faults_.try_emplace(id, std::move(fault));
    return inserted;
}

bool FaultRegistry::activate(const FaultId& id, const FaultConfig& config) {
    auto it = faults_.find(id);
    if (it == faults_.end()) {
        return false;
    }
    bool result = it->second->activate(config);
    if (result) {
        activations_[id] = ActivationInfo{config, 0};
        if (Logger::is_initialized()) {
            Logger::instance().event("fault", "Fault activated", {
                {"fault_id", id},
                {"target_zone_id", config.target_zone_id},
                {"duration_ticks", config.duration_ticks}
            });
        }
    }
    return result;
}

bool FaultRegistry::deactivate(const FaultId& id) {
    auto it = faults_.find(id);
    if (it == faults_.end()) {
        return false;
    }
    it->second->deactivate();
    activations_.erase(id);
    if (Logger::is_initialized()) {
        Logger::instance().event("fault", "Fault deactivated", {
            {"fault_id", id}
        });
    }
    return true;
}

void FaultRegistry::deactivate_all() {
    for (auto& [id, fault] : faults_) {
        if (fault->is_active()) {
            fault->deactivate();
            if (Logger::is_initialized()) {
                Logger::instance().event("fault", "Fault deactivated", {
                    {"fault_id", id}
                });
            }
        }
    }
    activations_.clear();
}

bool FaultRegistry::is_active(const FaultId& id) const {
    auto it = faults_.find(id);
    if (it == faults_.end()) {
        return false;
    }
    return it->second->is_active();
}

std::optional<FaultStatus> FaultRegistry::fault_status(const FaultId& id) const {
    auto it = faults_.find(id);
    if (it == faults_.end()) {
        return std::nullopt;
    }
    return it->second->status();
}

std::vector<FaultStatus> FaultRegistry::all_status() const {
    std::vector<FaultStatus> result;
    result.reserve(faults_.size());
    for (const auto& [id, fault] : faults_) {
        result.push_back(fault->status());
    }
    return result;
}

std::vector<FaultId> FaultRegistry::registered_ids() const {
    std::vector<FaultId> result;
    result.reserve(faults_.size());
    for (const auto& [id, fault] : faults_) {
        result.push_back(id);
    }
    return result;
}

size_t FaultRegistry::fault_count() const {
    return faults_.size();
}

size_t FaultRegistry::active_count() const {
    size_t count = 0;
    for (const auto& [id, fault] : faults_) {
        if (fault->is_active()) {
            ++count;
        }
    }
    return count;
}

void FaultRegistry::on_tick(uint64_t current_tick) {
    current_tick_ = current_tick;

    // Collect IDs to deactivate after iteration
    std::vector<FaultId> to_deactivate;

    for (auto& [id, info] : activations_) {
        auto fault_it = faults_.find(id);
        if (fault_it == faults_.end() || !fault_it->second->is_active()) {
            continue;
        }

        // Tick ambient faults (tick-scoped faults fire via execute_pre_tick_faults)
        if (fault_it->second->mode() == FaultMode::AMBIENT) {
            fault_it->second->on_tick(current_tick, nullptr);
        }

        // Duration tracking
        ++info.ticks_elapsed;
        if (info.config.duration_ticks > 0 &&
            info.ticks_elapsed >= info.config.duration_ticks) {
            to_deactivate.push_back(id);
        }
    }

    // Auto-deactivate expired faults
    for (const auto& id : to_deactivate) {
        deactivate(id);
    }
}

void FaultRegistry::execute_pre_tick_faults(Zone& /*zone*/) {
    // Stub — implemented in Commit 6
}

}  // namespace wow
