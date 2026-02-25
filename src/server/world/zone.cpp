#include "server/world/zone.h"

#include <chrono>
#include <stdexcept>

#include "server/telemetry/logger.h"

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

std::optional<Entity> Zone::take_entity(uint64_t session_id) {
    auto node = entities_.extract(session_id);
    if (node.empty()) {
        return std::nullopt;
    }
    return std::move(node.mapped());
}

bool Zone::has_entity(uint64_t session_id) const {
    return entities_.count(session_id) > 0;
}

size_t Zone::entity_count() const { return entities_.size(); }

const std::unordered_map<uint64_t, Entity>& Zone::entities() const {
    return entities_;
}

void Zone::push_event(std::unique_ptr<GameEvent> event) {
    event_queue_.push(std::move(event));
}

size_t Zone::event_queue_depth() const {
    return event_queue_.size();
}

ZoneTickResult Zone::tick(uint64_t current_tick) {
    auto start = std::chrono::steady_clock::now();

    ZoneTickResult result;
    result.zone_id = config_.zone_id;
    result.tick = current_tick;

    try {
        // Pre-tick hook (fault injection point)
        if (pre_tick_hook_) {
            pre_tick_hook_();
        }

        // Drain events from queue
        auto events = event_queue_.drain();
        result.events_processed = events.size();

        // Phase pipeline: Movement -> SpellCast -> Combat
        result.entities_moved = movement_processor_.process(events, entities_);
        result.spell_result = spellcast_processor_.process(events, entities_, current_tick);
        result.combat_result = combat_processor_.process(events, entities_);

        // Post-tick hook (fault injection point)
        if (post_tick_hook_) {
            post_tick_hook_();
        }

        // State recovery: CRASHED -> DEGRADED -> ACTIVE
        if (state_ == ZoneState::CRASHED) {
            state_ = ZoneState::DEGRADED;
        } else if (state_ == ZoneState::DEGRADED) {
            state_ = ZoneState::ACTIVE;
        }

    } catch (const std::exception& e) {
        result.had_error = true;
        result.error_message = e.what();
        state_ = ZoneState::CRASHED;
        ++error_count_;

        if (Logger::is_initialized()) {
            Logger::instance().error("zone", "Zone tick exception", {
                {"zone_id", config_.zone_id},
                {"zone_name", config_.name},
                {"tick", current_tick},
                {"error", e.what()}
            });
        }
    } catch (...) {
        result.had_error = true;
        result.error_message = "Unknown exception";
        state_ = ZoneState::CRASHED;
        ++error_count_;

        if (Logger::is_initialized()) {
            Logger::instance().error("zone", "Zone tick exception", {
                {"zone_id", config_.zone_id},
                {"zone_name", config_.name},
                {"tick", current_tick},
                {"error", "Unknown exception"}
            });
        }
    }

    ++total_ticks_;

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start);
    last_tick_duration_ms_ = duration.count();
    result.duration_ms = last_tick_duration_ms_;

    // Emit tick completion telemetry (outside exception guard — always fires)
    if (!result.had_error && Logger::is_initialized()) {
        Logger::instance().metric("zone", "Zone tick completed", {
            {"zone_id", config_.zone_id},
            {"zone_name", config_.name},
            {"tick", current_tick},
            {"events_processed", result.events_processed},
            {"entities_moved", result.entities_moved},
            {"duration_ms", result.duration_ms},
            {"casts_started", result.spell_result.casts_started},
            {"casts_completed", result.spell_result.casts_completed},
            {"casts_interrupted", result.spell_result.casts_interrupted},
            {"gcd_blocked", result.spell_result.gcd_blocked},
            {"attacks_processed", result.combat_result.attacks_processed},
            {"total_damage_dealt", result.combat_result.total_damage_dealt},
            {"kills", result.combat_result.kills},
        });
    }

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
