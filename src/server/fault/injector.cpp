#include "server/fault/injector.h"

namespace wow {

bool FaultRegistry::register_fault(std::unique_ptr<Fault> /*fault*/) {
    return false;
}

bool FaultRegistry::activate(const FaultId& /*id*/, const FaultConfig& /*config*/) {
    return false;
}

bool FaultRegistry::deactivate(const FaultId& /*id*/) {
    return false;
}

void FaultRegistry::deactivate_all() {}

bool FaultRegistry::is_active(const FaultId& /*id*/) const {
    return false;
}

std::optional<FaultStatus> FaultRegistry::fault_status(const FaultId& /*id*/) const {
    return std::nullopt;
}

std::vector<FaultStatus> FaultRegistry::all_status() const {
    return {};
}

std::vector<FaultId> FaultRegistry::registered_ids() const {
    return {};
}

size_t FaultRegistry::fault_count() const {
    return 0;
}

size_t FaultRegistry::active_count() const {
    return 0;
}

void FaultRegistry::on_tick(uint64_t /*current_tick*/) {}

void FaultRegistry::execute_pre_tick_faults(Zone& /*zone*/) {}

}  // namespace wow
