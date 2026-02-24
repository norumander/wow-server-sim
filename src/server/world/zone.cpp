#include "server/world/zone.h"

namespace wow {

Zone::Zone(const ZoneConfig& config)
    : config_(config) {}

ZoneId Zone::zone_id() const { return config_.zone_id; }

std::string_view Zone::name() const { return config_.name; }

ZoneState Zone::state() const { return state_; }

bool Zone::add_entity(Entity entity) {
    auto session_id = entity.session_id();
    auto [it, inserted] = entities_.try_emplace(session_id, std::move(entity));
    return inserted;
}

bool Zone::remove_entity(uint64_t session_id) {
    return entities_.erase(session_id) > 0;
}

std::optional<Entity> Zone::take_entity(uint64_t /*session_id*/) {
    // Stub — implemented in Commit 2
    return std::nullopt;
}

bool Zone::has_entity(uint64_t session_id) const {
    return entities_.count(session_id) > 0;
}

size_t Zone::entity_count() const { return entities_.size(); }

const std::unordered_map<uint64_t, Entity>& Zone::entities() const {
    return entities_;
}

void Zone::push_event(std::unique_ptr<GameEvent> /*event*/) {
    // Stub — implemented in Commit 2
}

size_t Zone::event_queue_depth() const {
    return event_queue_.size();
}

ZoneTickResult Zone::tick(uint64_t current_tick) {
    ZoneTickResult result;
    result.zone_id = config_.zone_id;
    result.tick = current_tick;
    ++total_ticks_;
    return result;
}

void Zone::set_pre_tick_hook(std::function<void()> hook) {
    pre_tick_hook_ = std::move(hook);
}

void Zone::set_post_tick_hook(std::function<void()> hook) {
    post_tick_hook_ = std::move(hook);
}

ZoneHealth Zone::health() const {
    return ZoneHealth{
        config_.zone_id,
        state_,
        total_ticks_,
        error_count_,
        entities_.size(),
        event_queue_.size(),
        last_tick_duration_ms_
    };
}

}  // namespace wow
