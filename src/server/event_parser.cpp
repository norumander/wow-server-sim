#include "server/event_parser.h"

#include "server/events/combat.h"
#include "server/events/movement.h"
#include "server/events/spellcast.h"

namespace wow {

std::unique_ptr<GameEvent> EventParser::parse(const nlohmann::json& obj)
{
    // Require top-level "type" and "session_id" fields.
    if (!obj.contains("type") || !obj.contains("session_id")) {
        return nullptr;
    }

    try {
        auto type = obj.at("type").get<std::string>();
        auto session_id = obj.at("session_id").get<uint64_t>();

        if (type == "movement") {
            return parse_movement(obj, session_id);
        } else if (type == "spell_cast") {
            return parse_spell_cast(obj, session_id);
        } else if (type == "combat") {
            return parse_combat(obj, session_id);
        }
        // Unknown type — return nullptr for caller to log and drop.
        return nullptr;

    } catch (const nlohmann::json::exception&) {
        // Type mismatch or missing field — malformed payload.
        return nullptr;
    }
}

std::unique_ptr<GameEvent> EventParser::parse_movement(
    const nlohmann::json& obj, uint64_t session_id)
{
    if (!obj.contains("position")) {
        return nullptr;
    }
    const auto& pos = obj.at("position");
    if (!pos.contains("x") || !pos.contains("y") || !pos.contains("z")) {
        return nullptr;
    }

    Position position;
    position.x = pos.at("x").get<float>();
    position.y = pos.at("y").get<float>();
    position.z = pos.at("z").get<float>();

    return std::make_unique<MovementEvent>(session_id, position);
}

std::unique_ptr<GameEvent> EventParser::parse_spell_cast(
    const nlohmann::json& obj, uint64_t session_id)
{
    if (!obj.contains("action")) {
        return nullptr;
    }

    auto action_str = obj.at("action").get<std::string>();

    if (action_str == "CAST_START") {
        if (!obj.contains("spell_id") || !obj.contains("cast_time_ticks")) {
            return nullptr;
        }
        auto spell_id = obj.at("spell_id").get<uint32_t>();
        auto cast_time = obj.at("cast_time_ticks").get<uint32_t>();
        return std::make_unique<SpellCastEvent>(
            session_id, SpellAction::CAST_START, spell_id, cast_time);
    } else if (action_str == "INTERRUPT") {
        return std::make_unique<SpellCastEvent>(
            session_id, SpellAction::INTERRUPT, 0, 0);
    }
    // Unknown spell action.
    return nullptr;
}

std::unique_ptr<GameEvent> EventParser::parse_combat(
    const nlohmann::json& obj, uint64_t session_id)
{
    if (!obj.contains("action") || !obj.contains("target_session_id") ||
        !obj.contains("base_damage") || !obj.contains("damage_type")) {
        return nullptr;
    }

    auto action_str = obj.at("action").get<std::string>();
    if (action_str != "ATTACK") {
        return nullptr;
    }

    auto target_id = obj.at("target_session_id").get<uint64_t>();
    auto base_damage = obj.at("base_damage").get<int32_t>();
    auto damage_type_str = obj.at("damage_type").get<std::string>();

    DamageType damage_type;
    if (damage_type_str == "PHYSICAL") {
        damage_type = DamageType::PHYSICAL;
    } else if (damage_type_str == "MAGICAL") {
        damage_type = DamageType::MAGICAL;
    } else {
        return nullptr;
    }

    return std::make_unique<CombatEvent>(
        session_id, CombatAction::ATTACK, target_id, base_damage, damage_type);
}

}  // namespace wow
