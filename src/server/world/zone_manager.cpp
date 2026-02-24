#include "server/world/zone_manager.h"

namespace wow {

ZoneId ZoneManager::create_zone(const ZoneConfig& config) {
    auto zone = std::make_unique<Zone>(config);
    auto id = config.zone_id;
    zones_.emplace(id, std::move(zone));
    return id;
}

Zone* ZoneManager::get_zone(ZoneId zone_id) {
    auto it = zones_.find(zone_id);
    return it != zones_.end() ? it->second.get() : nullptr;
}

const Zone* ZoneManager::get_zone(ZoneId zone_id) const {
    auto it = zones_.find(zone_id);
    return it != zones_.end() ? it->second.get() : nullptr;
}

size_t ZoneManager::zone_count() const {
    return zones_.size();
}

bool ZoneManager::assign_session(uint64_t /*session_id*/, ZoneId /*zone_id*/) {
    // Stub — implemented in Commit 4
    return false;
}

bool ZoneManager::remove_session(uint64_t /*session_id*/) {
    // Stub — implemented in Commit 4
    return false;
}

bool ZoneManager::transfer_session(uint64_t /*session_id*/, ZoneId /*target_zone_id*/) {
    // Stub — implemented in Commit 4
    return false;
}

ZoneId ZoneManager::session_zone(uint64_t /*session_id*/) const {
    // Stub — implemented in Commit 4
    return kNoZone;
}

size_t ZoneManager::route_events(std::vector<std::unique_ptr<GameEvent>>& /*events*/) {
    // Stub — implemented in Commit 4
    return 0;
}

ZoneManagerTickResult ZoneManager::tick_all(uint64_t /*current_tick*/) {
    // Stub — implemented in Commit 4
    return {};
}

}  // namespace wow
