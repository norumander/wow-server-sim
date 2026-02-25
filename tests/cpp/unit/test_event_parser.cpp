#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>

#include "server/event_parser.h"
#include "server/events/combat.h"
#include "server/events/event.h"
#include "server/events/movement.h"
#include "server/events/spellcast.h"

using json = nlohmann::json;

// ===========================================================================
// A. Valid event parsing
// ===========================================================================

TEST(EventParser, ValidMovementEvent)
{
    auto j = json{
        {"type", "movement"},
        {"session_id", 1},
        {"position", {{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}}};

    auto event = wow::EventParser::parse(j);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->event_type(), wow::EventType::MOVEMENT);
    EXPECT_EQ(event->session_id(), 1u);

    auto* move = static_cast<wow::MovementEvent*>(event.get());
    EXPECT_FLOAT_EQ(move->position().x, 1.0f);
    EXPECT_FLOAT_EQ(move->position().y, 2.0f);
    EXPECT_FLOAT_EQ(move->position().z, 3.0f);
}

TEST(EventParser, ValidSpellCastStart)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"action", "CAST_START"},
        {"spell_id", 42},
        {"cast_time_ticks", 20}};

    auto event = wow::EventParser::parse(j);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->event_type(), wow::EventType::SPELL_CAST);
    EXPECT_EQ(event->session_id(), 1u);

    auto* spell = static_cast<wow::SpellCastEvent*>(event.get());
    EXPECT_EQ(spell->action(), wow::SpellAction::CAST_START);
    EXPECT_EQ(spell->spell_id(), 42u);
    EXPECT_EQ(spell->cast_time_ticks(), 20u);
}

TEST(EventParser, ValidSpellCastInterrupt)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"action", "INTERRUPT"}};

    auto event = wow::EventParser::parse(j);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->event_type(), wow::EventType::SPELL_CAST);

    auto* spell = static_cast<wow::SpellCastEvent*>(event.get());
    EXPECT_EQ(spell->action(), wow::SpellAction::INTERRUPT);
    EXPECT_EQ(spell->spell_id(), 0u);
    EXPECT_EQ(spell->cast_time_ticks(), 0u);
}

TEST(EventParser, ValidCombatPhysical)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"target_session_id", 2},
        {"base_damage", 30},
        {"damage_type", "PHYSICAL"}};

    auto event = wow::EventParser::parse(j);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->event_type(), wow::EventType::COMBAT);
    EXPECT_EQ(event->session_id(), 1u);

    auto* combat = static_cast<wow::CombatEvent*>(event.get());
    EXPECT_EQ(combat->action(), wow::CombatAction::ATTACK);
    EXPECT_EQ(combat->target_session_id(), 2u);
    EXPECT_EQ(combat->base_damage(), 30);
    EXPECT_EQ(combat->damage_type(), wow::DamageType::PHYSICAL);
}

TEST(EventParser, ValidCombatMagical)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"target_session_id", 2},
        {"base_damage", 50},
        {"damage_type", "MAGICAL"}};

    auto event = wow::EventParser::parse(j);
    ASSERT_NE(event, nullptr);

    auto* combat = static_cast<wow::CombatEvent*>(event.get());
    EXPECT_EQ(combat->damage_type(), wow::DamageType::MAGICAL);
}

// ===========================================================================
// B. Unknown / missing type field
// ===========================================================================

TEST(EventParser, UnknownTypeReturnsNullptr)
{
    auto j = json{{"type", "unknown"}, {"session_id", 1}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, MissingTypeFieldReturnsNullptr)
{
    auto j = json{{"session_id", 1}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

// ===========================================================================
// C. Missing session_id
// ===========================================================================

TEST(EventParser, MissingSessionIdReturnsNullptr)
{
    auto j = json{
        {"type", "movement"},
        {"position", {{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

// ===========================================================================
// D. Missing required fields per event type
// ===========================================================================

TEST(EventParser, MovementMissingPositionReturnsNullptr)
{
    auto j = json{{"type", "movement"}, {"session_id", 1}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, MovementMissingPositionFieldReturnsNullptr)
{
    // position object exists but missing z
    auto j = json{
        {"type", "movement"},
        {"session_id", 1},
        {"position", {{"x", 1.0}, {"y", 2.0}}}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, SpellCastMissingActionReturnsNullptr)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"spell_id", 42},
        {"cast_time_ticks", 20}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, SpellCastStartMissingSpellIdReturnsNullptr)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"action", "CAST_START"},
        {"cast_time_ticks", 20}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, SpellCastStartMissingCastTimeReturnsNullptr)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"action", "CAST_START"},
        {"spell_id", 42}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatMissingActionReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"target_session_id", 2},
        {"base_damage", 30},
        {"damage_type", "PHYSICAL"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatMissingTargetReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"base_damage", 30},
        {"damage_type", "PHYSICAL"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatMissingBaseDamageReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"target_session_id", 2},
        {"damage_type", "PHYSICAL"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatMissingDamageTypeReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"target_session_id", 2},
        {"base_damage", 30}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatUnknownDamageTypeReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "ATTACK"},
        {"target_session_id", 2},
        {"base_damage", 30},
        {"damage_type", "SHADOW"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, SpellCastUnknownActionReturnsNullptr)
{
    auto j = json{
        {"type", "spell_cast"},
        {"session_id", 1},
        {"action", "CHANNEL"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}

TEST(EventParser, CombatUnknownActionReturnsNullptr)
{
    auto j = json{
        {"type", "combat"},
        {"session_id", 1},
        {"action", "HEAL"},
        {"target_session_id", 2},
        {"base_damage", 30},
        {"damage_type", "PHYSICAL"}};
    EXPECT_EQ(wow::EventParser::parse(j), nullptr);
}
