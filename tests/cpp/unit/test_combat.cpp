#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/events/combat.h"
#include "server/events/event.h"
#include "server/telemetry/logger.h"
#include "server/world/entity.h"

using json = nlohmann::json;

// ===========================================================================
// Test fixture — mirrors SpellCastTest pattern with telemetry sink
// ===========================================================================

class CombatTest : public ::testing::Test {
protected:
    std::ostringstream sink_;

    void SetUp() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    void TearDown() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    /// Initialize the telemetry logger with our test sink.
    void init_logger()
    {
        wow::LoggerConfig config;
        config.custom_sink = &sink_;
        wow::Logger::initialize(config);
    }

    /// Parse all JSON lines from the sink into a vector.
    std::vector<json> parse_all_lines()
    {
        std::vector<json> entries;
        std::istringstream stream(sink_.str());
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                entries.push_back(json::parse(line));
            }
        }
        return entries;
    }

    /// Filter log entries by message substring.
    std::vector<json> filter_by_message(const std::string& substr)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            auto msg = entry.value("message", "");
            if (msg.find(substr) != std::string::npos) {
                result.push_back(entry);
            }
        }
        return result;
    }

    /// Helper: create an entity map with one player entity.
    std::unordered_map<uint64_t, wow::Entity> make_entities(uint64_t session_id)
    {
        std::unordered_map<uint64_t, wow::Entity> entities;
        entities.emplace(session_id, wow::Entity{session_id});
        return entities;
    }

    /// Helper: create an ATTACK combat event.
    std::unique_ptr<wow::GameEvent> make_attack(uint64_t attacker_id,
                                                 uint64_t target_id,
                                                 int32_t base_damage,
                                                 wow::DamageType damage_type = wow::DamageType::PHYSICAL)
    {
        return std::make_unique<wow::CombatEvent>(
            attacker_id, wow::CombatAction::ATTACK, target_id, base_damage, damage_type);
    }
};

// ===========================================================================
// Group A: CombatEvent Data (3 tests)
// ===========================================================================

TEST_F(CombatTest, CombatEvent_HasCombatType)
{
    wow::CombatEvent evt(1, wow::CombatAction::ATTACK, 2, 50, wow::DamageType::PHYSICAL);
    EXPECT_EQ(evt.event_type(), wow::EventType::COMBAT);
}

TEST_F(CombatTest, CombatEvent_StoresAttackFields)
{
    wow::CombatEvent evt(10, wow::CombatAction::ATTACK, 20, 75, wow::DamageType::PHYSICAL);

    EXPECT_EQ(evt.session_id(), 10u);
    EXPECT_EQ(evt.action(), wow::CombatAction::ATTACK);
    EXPECT_EQ(evt.target_session_id(), 20u);
    EXPECT_EQ(evt.base_damage(), 75);
    EXPECT_EQ(evt.damage_type(), wow::DamageType::PHYSICAL);
}

TEST_F(CombatTest, CombatEvent_StoresPhysicalAndMagicalTypes)
{
    wow::CombatEvent phys(1, wow::CombatAction::ATTACK, 2, 10, wow::DamageType::PHYSICAL);
    wow::CombatEvent mag(1, wow::CombatAction::ATTACK, 2, 10, wow::DamageType::MAGICAL);

    EXPECT_EQ(phys.damage_type(), wow::DamageType::PHYSICAL);
    EXPECT_EQ(mag.damage_type(), wow::DamageType::MAGICAL);
}

// ===========================================================================
// Group B: CombatState and Entity (3 tests)
// ===========================================================================

TEST_F(CombatTest, Entity_CombatStateDefaultValues)
{
    wow::Entity entity(1);
    const auto& cs = entity.combat_state();

    EXPECT_EQ(cs.health, 100);
    EXPECT_EQ(cs.max_health, 100);
    EXPECT_FLOAT_EQ(cs.armor, 0.0f);
    EXPECT_FLOAT_EQ(cs.resistance, 0.0f);
    EXPECT_TRUE(cs.is_alive);
    EXPECT_EQ(cs.base_attack_damage, 0);
    EXPECT_TRUE(cs.threat_table.empty());
}

TEST_F(CombatTest, Entity_CombatStateMutableAccess)
{
    wow::Entity entity(1);

    // Write via mutable accessor.
    entity.combat_state().health = 50;
    entity.combat_state().max_health = 200;
    entity.combat_state().armor = 0.25f;
    entity.combat_state().resistance = 0.50f;
    entity.combat_state().is_alive = false;
    entity.combat_state().base_attack_damage = 30;
    entity.combat_state().threat_table[42] = 100.0f;

    // Read back via const accessor.
    const wow::Entity& cref = entity;
    EXPECT_EQ(cref.combat_state().health, 50);
    EXPECT_EQ(cref.combat_state().max_health, 200);
    EXPECT_FLOAT_EQ(cref.combat_state().armor, 0.25f);
    EXPECT_FLOAT_EQ(cref.combat_state().resistance, 0.50f);
    EXPECT_FALSE(cref.combat_state().is_alive);
    EXPECT_EQ(cref.combat_state().base_attack_damage, 30);
    EXPECT_FLOAT_EQ(cref.combat_state().threat_table.at(42), 100.0f);
}

TEST_F(CombatTest, Entity_EntityTypeDefaultsToPlayer)
{
    wow::Entity player(1);
    EXPECT_EQ(player.entity_type(), wow::EntityType::PLAYER);

    wow::Entity npc(1000000, wow::EntityType::NPC);
    EXPECT_EQ(npc.entity_type(), wow::EntityType::NPC);
}

// ===========================================================================
// Group C: Basic Damage Application (3 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_PhysicalDamageAppliesArmorReduction)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().armor = 0.25f;  // 25% reduction

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 40, wow::DamageType::PHYSICAL));

    auto result = processor.process(events, entities);

    // 40 * (1 - 0.25) = 30 actual damage, health 100 → 70
    EXPECT_EQ(result.attacks_processed, 1u);
    EXPECT_EQ(entities.at(2).combat_state().health, 70);
}

TEST_F(CombatTest, Processor_MagicalDamageAppliesResistanceReduction)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().resistance = 0.50f;  // 50% reduction

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 60, wow::DamageType::MAGICAL));

    auto result = processor.process(events, entities);

    // 60 * (1 - 0.50) = 30 actual damage, health 100 → 70
    EXPECT_EQ(result.attacks_processed, 1u);
    EXPECT_EQ(entities.at(2).combat_state().health, 70);
}

TEST_F(CombatTest, Processor_ZeroArmorFullDamage)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    // armor defaults to 0.0

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 50, wow::DamageType::PHYSICAL));

    auto result = processor.process(events, entities);

    // 50 * (1 - 0.0) = 50 actual damage, health 100 → 50
    EXPECT_EQ(result.attacks_processed, 1u);
    EXPECT_EQ(entities.at(2).combat_state().health, 50);
}

// ===========================================================================
// Group D: Attack Validation (4 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_AttackOnUnknownTargetSkips)
{
    init_logger();
    wow::CombatProcessor processor;

    auto entities = make_entities(1);  // only attacker exists

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 99, 50));  // target 99 doesn't exist

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.attacks_missed, 1u);
    EXPECT_EQ(result.attacks_processed, 0u);
}

TEST_F(CombatTest, Processor_AttackFromUnknownAttackerSkips)
{
    init_logger();
    wow::CombatProcessor processor;

    auto entities = make_entities(2);  // only target exists

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(99, 2, 50));  // attacker 99 doesn't exist

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.attacks_missed, 1u);
    EXPECT_EQ(result.attacks_processed, 0u);
}

TEST_F(CombatTest, Processor_AttackOnDeadTargetSkips)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().is_alive = false;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 50));

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.attacks_missed, 1u);
    EXPECT_EQ(result.attacks_processed, 0u);
}

TEST_F(CombatTest, Processor_AttackFromDeadAttackerSkips)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(1).combat_state().is_alive = false;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 50));

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.attacks_missed, 1u);
    EXPECT_EQ(result.attacks_processed, 0u);
}

// ===========================================================================
// Group E: Kill and Death (3 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_TargetDiesAtZeroHealth)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    // 100 HP, 0 armor, 100 base damage → exactly 0 HP

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 100));

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.kills, 1u);
    EXPECT_FALSE(entities.at(2).combat_state().is_alive);
    EXPECT_LE(entities.at(2).combat_state().health, 0);
}

TEST_F(CombatTest, Processor_OverkillDamageStillKills)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().health = 50;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 200));

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.kills, 1u);
    EXPECT_FALSE(entities.at(2).combat_state().is_alive);
}

TEST_F(CombatTest, Processor_SecondAttackOnNewlyDeadSkips)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(3, wow::Entity{3});
    entities.emplace(2, wow::Entity{2});
    // Two attacks in same tick: first kills, second should skip.

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 100));  // kills target (100 HP, 0 armor)
    events.push_back(make_attack(3, 2, 50));   // target already dead

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.attacks_processed, 1u);
    EXPECT_EQ(result.attacks_missed, 1u);
    EXPECT_EQ(result.kills, 1u);
}

// ===========================================================================
// Group F: Threat Table (3 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_DamageGeneratesThreat)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().armor = 0.25f;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 40));  // 40 * 0.75 = 30 actual

    processor.process(events, entities);

    // Threat = actual damage dealt = 30
    const auto& threat = entities.at(2).combat_state().threat_table;
    ASSERT_TRUE(threat.count(1) > 0);
    EXPECT_FLOAT_EQ(threat.at(1), 30.0f);
}

TEST_F(CombatTest, Processor_MultipleAttacksAccumulateThreat)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().health = 500;
    entities.at(2).combat_state().max_health = 500;

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 20));  // 20 damage (0 armor)
    events.push_back(make_attack(1, 2, 30));  // 30 damage (0 armor)

    processor.process(events, entities);

    // Total threat = 20 + 30 = 50
    const auto& threat = entities.at(2).combat_state().threat_table;
    ASSERT_TRUE(threat.count(1) > 0);
    EXPECT_FLOAT_EQ(threat.at(1), 50.0f);
}

TEST_F(CombatTest, Processor_DeadEntityRemovedFromThreatTables)
{
    init_logger();
    wow::CombatProcessor processor;

    // Set up: entity 2 has entity 1 in its threat table, then entity 1 dies.
    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.emplace(3, wow::Entity{3});

    // Pre-populate threat: entity 2 has threat from entity 1
    entities.at(2).combat_state().threat_table[1] = 50.0f;
    entities.at(2).combat_state().health = 500;

    // Entity 3 kills entity 1 (100 HP, 0 armor, 100 damage)
    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(3, 1, 100));

    processor.process(events, entities);

    // Entity 1 is dead — should be removed from entity 2's threat table
    EXPECT_FALSE(entities.at(1).combat_state().is_alive);
    EXPECT_EQ(entities.at(2).combat_state().threat_table.count(1), 0u);
}

// ===========================================================================
// Group G: NPC Auto-Attack (3 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_NpcAttacksHighestThreatTarget)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    // Two players
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    // One NPC boss
    entities.emplace(1000000, wow::Entity{1000000, wow::EntityType::NPC});
    entities.at(1000000).combat_state().base_attack_damage = 20;
    entities.at(1000000).combat_state().health = 500;
    entities.at(1000000).combat_state().max_health = 500;
    // Player 1 has higher threat than player 2
    entities.at(1000000).combat_state().threat_table[1] = 100.0f;
    entities.at(1000000).combat_state().threat_table[2] = 50.0f;

    std::vector<std::unique_ptr<wow::GameEvent>> events;  // no player attacks

    auto result = processor.process(events, entities);

    // NPC should attack player 1 (highest threat)
    EXPECT_GE(result.npc_attacks, 1u);
    // Player 1 took damage: 20 * (1 - 0) = 20, health 100 → 80
    EXPECT_EQ(entities.at(1).combat_state().health, 80);
    // Player 2 should be unharmed
    EXPECT_EQ(entities.at(2).combat_state().health, 100);
}

TEST_F(CombatTest, Processor_NpcWithNoThreatDoesNotAttack)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(1000000, wow::Entity{1000000, wow::EntityType::NPC});
    entities.at(1000000).combat_state().base_attack_damage = 20;
    // Empty threat table — NPC should not attack

    std::vector<std::unique_ptr<wow::GameEvent>> events;

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.npc_attacks, 0u);
    EXPECT_EQ(entities.at(1).combat_state().health, 100);
}

TEST_F(CombatTest, Processor_DeadNpcDoesNotAttack)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(1000000, wow::Entity{1000000, wow::EntityType::NPC});
    entities.at(1000000).combat_state().base_attack_damage = 20;
    entities.at(1000000).combat_state().is_alive = false;
    entities.at(1000000).combat_state().threat_table[1] = 100.0f;

    std::vector<std::unique_ptr<wow::GameEvent>> events;

    auto result = processor.process(events, entities);

    EXPECT_EQ(result.npc_attacks, 0u);
    EXPECT_EQ(entities.at(1).combat_state().health, 100);
}

// ===========================================================================
// Group H: Telemetry + Integration (4 tests)
// ===========================================================================

TEST_F(CombatTest, Processor_EmitsTelemetryOnDamageDealt)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 40, wow::DamageType::PHYSICAL));

    processor.process(events, entities);

    auto entries = filter_by_message("Damage dealt");
    ASSERT_GE(entries.size(), 1u);

    const auto& data = entries[0]["data"];
    EXPECT_EQ(data["attacker_id"], 1u);
    EXPECT_EQ(data["target_id"], 2u);
    EXPECT_EQ(data["actual_damage"], 40);
    EXPECT_EQ(data["damage_type"], "physical");
}

TEST_F(CombatTest, Processor_EmitsTelemetryOnKill)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});

    std::vector<std::unique_ptr<wow::GameEvent>> events;
    events.push_back(make_attack(1, 2, 100));

    processor.process(events, entities);

    auto entries = filter_by_message("Entity killed");
    ASSERT_GE(entries.size(), 1u);

    const auto& data = entries[0]["data"];
    EXPECT_EQ(data["target_id"], 2u);
    EXPECT_EQ(data["killer_id"], 1u);
}

TEST_F(CombatTest, TickIntegration_FullCombatLifecycle)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    entities.emplace(1, wow::Entity{1});
    entities.emplace(2, wow::Entity{2});
    entities.at(2).combat_state().armor = 0.50f;
    entities.at(2).combat_state().health = 200;
    entities.at(2).combat_state().max_health = 200;

    // Tick 1: Attack for 100 base → 50 actual (50% armor). Health 200 → 150.
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        events.push_back(make_attack(1, 2, 100));
        auto result = processor.process(events, entities);
        EXPECT_EQ(result.attacks_processed, 1u);
        EXPECT_EQ(result.kills, 0u);
        EXPECT_EQ(entities.at(2).combat_state().health, 150);
    }

    // Tick 2: Attack for 200 base → 100 actual. Health 150 → 50.
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        events.push_back(make_attack(1, 2, 200));
        auto result = processor.process(events, entities);
        EXPECT_EQ(result.attacks_processed, 1u);
        EXPECT_EQ(result.kills, 0u);
        EXPECT_EQ(entities.at(2).combat_state().health, 50);
    }

    // Tick 3: Killing blow — 200 base → 100 actual. Health 50 → -50. Dead.
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        events.push_back(make_attack(1, 2, 200));
        auto result = processor.process(events, entities);
        EXPECT_EQ(result.attacks_processed, 1u);
        EXPECT_EQ(result.kills, 1u);
        EXPECT_FALSE(entities.at(2).combat_state().is_alive);
    }

    // Verify telemetry trail.
    auto damage_entries = filter_by_message("Damage dealt");
    auto kill_entries = filter_by_message("Entity killed");
    EXPECT_EQ(damage_entries.size(), 3u);
    EXPECT_EQ(kill_entries.size(), 1u);
}

TEST_F(CombatTest, TickIntegration_BossFightScenario)
{
    init_logger();
    wow::CombatProcessor processor;

    std::unordered_map<uint64_t, wow::Entity> entities;
    // Two players (tank=1, dps=2)
    entities.emplace(1, wow::Entity{1});
    entities.at(1).combat_state().armor = 0.50f;  // tank has armor
    entities.emplace(2, wow::Entity{2});
    // Boss NPC
    entities.emplace(1000000, wow::Entity{1000000, wow::EntityType::NPC});
    entities.at(1000000).combat_state().health = 1000;
    entities.at(1000000).combat_state().max_health = 1000;
    entities.at(1000000).combat_state().base_attack_damage = 30;

    // Tick 1: Tank attacks boss for 40 damage, DPS attacks boss for 20 damage.
    // Boss retaliates against tank (highest threat = 40 > 20).
    {
        std::vector<std::unique_ptr<wow::GameEvent>> events;
        events.push_back(make_attack(1, 1000000, 40));
        events.push_back(make_attack(2, 1000000, 20));

        auto result = processor.process(events, entities);

        EXPECT_EQ(result.attacks_processed, 2u);
        EXPECT_GE(result.npc_attacks, 1u);

        // Boss took 40 + 20 = 60 damage. Health 1000 → 940.
        EXPECT_EQ(entities.at(1000000).combat_state().health, 940);

        // Boss threat table: tank=40, dps=20
        const auto& threat = entities.at(1000000).combat_state().threat_table;
        EXPECT_FLOAT_EQ(threat.at(1), 40.0f);
        EXPECT_FLOAT_EQ(threat.at(2), 20.0f);

        // Tank took boss auto-attack: 30 * (1 - 0.50) = 15 damage. Health 100 → 85.
        EXPECT_EQ(entities.at(1).combat_state().health, 85);

        // DPS should be unharmed (not highest threat).
        EXPECT_EQ(entities.at(2).combat_state().health, 100);
    }
}
