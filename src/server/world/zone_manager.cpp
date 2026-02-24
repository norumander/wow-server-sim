#include "server/world/zone_manager.h"

#include "server/telemetry/logger.h"

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

bool ZoneManager::assign_session(uint64_t session_id, ZoneId zone_id) {
    // Check zone exists
    auto* zone = get_zone(zone_id);
    if (!zone) {
        return false;
    }

    // Check session not already assigned
    if (session_zone_map_.count(session_id) > 0) {
        return false;
    }

    // Create entity and add to zone
    Entity entity(session_id);
    if (!zone->add_entity(std::move(entity))) {
        return false;
    }

    session_zone_map_[session_id] = zone_id;
    return true;
}

bool ZoneManager::remove_session(uint64_t session_id) {
    auto it = session_zone_map_.find(session_id);
    if (it == session_zone_map_.end()) {
        return false;
    }

    auto* zone = get_zone(it->second);
    if (zone) {
        zone->remove_entity(session_id);
    }

    session_zone_map_.erase(it);
    return true;
}

bool ZoneManager::transfer_session(uint64_t session_id, ZoneId target_zone_id) {
    // Look up current zone
    auto it = session_zone_map_.find(session_id);
    if (it == session_zone_map_.end()) {
        return false;
    }

    auto* target_zone = get_zone(target_zone_id);
    if (!target_zone) {
        return false;
    }

    auto* source_zone = get_zone(it->second);
    if (!source_zone) {
        return false;
    }

    // Extract entity from source zone (preserves state)
    auto entity = source_zone->take_entity(session_id);
    if (!entity.has_value()) {
        return false;
    }

    // Add to target zone
    if (!target_zone->add_entity(std::move(*entity))) {
        // Rollback: put entity back in source zone
        source_zone->add_entity(std::move(*entity));
        return false;
    }

    session_zone_map_[session_id] = target_zone_id;
    return true;
}

ZoneId ZoneManager::session_zone(uint64_t session_id) const {
    auto it = session_zone_map_.find(session_id);
    if (it == session_zone_map_.end()) {
        return kNoZone;
    }
    return it->second;
}

size_t ZoneManager::route_events(std::vector<std::unique_ptr<GameEvent>>& events) {
    size_t routed = 0;

    for (auto& event : events) {
        if (!event) continue;

        auto it = session_zone_map_.find(event->session_id());
        if (it == session_zone_map_.end()) {
            // Unknown session — discard event
            if (Logger::is_initialized()) {
                Logger::instance().error("zone_manager",
                    "Event for unassigned session discarded", {
                        {"session_id", event->session_id()},
                        {"event_type", std::string(event_type_to_string(event->event_type()))}
                    });
            }
            continue;
        }

        auto* zone = get_zone(it->second);
        if (zone) {
            zone->push_event(std::move(event));
            ++routed;
        }
    }

    return routed;
}

ZoneManagerTickResult ZoneManager::tick_all(uint64_t current_tick) {
    ZoneManagerTickResult result;
    result.tick = current_tick;

    for (auto& [zone_id, zone] : zones_) {
        auto zone_result = zone->tick(current_tick);

        if (zone_result.had_error) {
            ++result.zones_with_errors;
        }

        result.total_events += zone_result.events_processed;
        result.zone_results.push_back(std::move(zone_result));
        ++result.zones_ticked;
    }

    return result;
}

}  // namespace wow
