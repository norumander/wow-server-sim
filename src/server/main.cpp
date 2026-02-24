#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "control/control_channel.h"
#include "server/fault/injector.h"
#include "server/fault/scenarios.h"
#include "server/game_loop.h"
#include "server/game_server.h"
#include "server/session_event_queue.h"
#include "server/telemetry/logger.h"
#include "server/world/entity.h"
#include "server/world/zone.h"
#include "server/world/zone_manager.h"

/// Global shutdown flag set by signal handler, checked by game loop.
static std::atomic<bool> g_shutdown_requested{false};

/// Signal handler for SIGINT (Ctrl+C) and SIGTERM.
static void signal_handler(int /*signum*/)
{
    g_shutdown_requested.store(true);
}

/// Entry point for the WoW Server Simulator.
///
/// Wires all subsystems: Logger, ZoneManager (with zones and NPCs),
/// FaultRegistry (F1-F4), SessionEventQueue, ControlChannel, GameServer,
/// and GameLoop. The game loop runs on the main thread at 20 Hz.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // -----------------------------------------------------------------------
    // 1. Telemetry Logger
    // -----------------------------------------------------------------------
    wow::LoggerConfig log_config;
    log_config.stdout_enabled = true;
    log_config.file_path = "telemetry.jsonl";
    wow::Logger::initialize(log_config);

    wow::Logger::instance().event("server", "Server starting", {
        {"version", "0.1.0"},
        {"tick_rate_hz", 20},
    });

    // -----------------------------------------------------------------------
    // 2. Zone Manager — create zones with NPCs
    // -----------------------------------------------------------------------
    wow::ZoneManager zone_manager;

    // Zone 1: Elwynn Forest with Hogger
    zone_manager.create_zone({1, "Elwynn Forest"});
    {
        wow::Entity hogger(1000001, wow::EntityType::NPC);
        hogger.combat_state().health = 150;
        hogger.combat_state().max_health = 150;
        hogger.combat_state().armor = 0.25f;
        hogger.combat_state().base_attack_damage = 15;
        zone_manager.get_zone(1)->add_entity(std::move(hogger));
    }

    // Zone 2: Westfall with Defias Pillager
    zone_manager.create_zone({2, "Westfall"});
    {
        wow::Entity pillager(1000002, wow::EntityType::NPC);
        pillager.combat_state().health = 100;
        pillager.combat_state().max_health = 100;
        pillager.combat_state().armor = 0.10f;
        pillager.combat_state().base_attack_damage = 10;
        zone_manager.get_zone(2)->add_entity(std::move(pillager));
    }

    wow::Logger::instance().event("server", "Zones initialized", {
        {"zone_count", zone_manager.zone_count()},
    });

    // -----------------------------------------------------------------------
    // 3. Fault Registry — register F1-F8 scenarios
    // -----------------------------------------------------------------------
    wow::FaultRegistry fault_registry;
    fault_registry.register_fault(std::make_unique<wow::LatencySpikeFault>());
    fault_registry.register_fault(std::make_unique<wow::SessionCrashFault>());
    fault_registry.register_fault(std::make_unique<wow::EventQueueFloodFault>());
    fault_registry.register_fault(std::make_unique<wow::MemoryPressureFault>());
    fault_registry.register_fault(std::make_unique<wow::CascadingZoneFailureFault>());
    fault_registry.register_fault(std::make_unique<wow::SlowLeakFault>());
    fault_registry.register_fault(std::make_unique<wow::SplitBrainFault>());
    fault_registry.register_fault(std::make_unique<wow::ThunderingHerdFault>());

    wow::Logger::instance().event("server", "Fault registry initialized", {
        {"fault_count", fault_registry.fault_count()},
    });

    // -----------------------------------------------------------------------
    // 4. Wire pre-tick hooks — fault injection fires inside zone exception guard
    // -----------------------------------------------------------------------
    for (uint32_t zid : {1u, 2u}) {
        auto* zone = zone_manager.get_zone(zid);
        zone->set_pre_tick_hook([&fault_registry, zone]() {
            fault_registry.execute_pre_tick_faults(*zone);
        });
    }

    // -----------------------------------------------------------------------
    // 5. Session Event Queue — bridges network → game thread
    // -----------------------------------------------------------------------
    wow::SessionEventQueue session_events;

    // -----------------------------------------------------------------------
    // 6. Control Channel — fault injection TCP server (port 8081)
    // -----------------------------------------------------------------------
    wow::ControlChannelConfig control_config;
    control_config.port = 8081;
    wow::ControlChannel control(fault_registry, control_config);
    control.start();

    wow::Logger::instance().event("server", "Control channel started", {
        {"port", control.port()},
    });

    // -----------------------------------------------------------------------
    // 7. Game Server — TCP accept for clients (port 8080)
    // -----------------------------------------------------------------------
    wow::GameServerConfig server_config;
    server_config.port = 8080;
    wow::GameServer game_server(server_config);
    game_server.set_session_event_queue(&session_events);
    game_server.start();

    wow::Logger::instance().event("server", "Game server started", {
        {"port", game_server.port()},
    });

    // -----------------------------------------------------------------------
    // 8. Signal handling — Ctrl+C triggers graceful shutdown
    // -----------------------------------------------------------------------
    std::signal(SIGINT, signal_handler);
#ifndef _WIN32
    std::signal(SIGTERM, signal_handler);
#endif

    // -----------------------------------------------------------------------
    // 9. Game Loop — 20 Hz tick on main thread
    // -----------------------------------------------------------------------
    wow::GameLoopConfig loop_config;
    loop_config.tick_rate_hz = 20.0;
    wow::GameLoop game_loop(loop_config);

    game_loop.on_tick([&](uint64_t tick) {
        // Check shutdown signal
        if (g_shutdown_requested.load()) {
            game_loop.stop();
            return;
        }

        // 1. Drain session events → assign/remove from zones
        auto session_evts = session_events.drain();
        for (const auto& evt : session_evts) {
            if (evt.type == wow::SessionEventType::CONNECTED) {
                // Round-robin: odd session_id → zone 1, even → zone 2
                wow::ZoneId target = (evt.session_id % 2 == 1) ? 1 : 2;
                zone_manager.assign_session(evt.session_id, target);
            } else {
                zone_manager.remove_session(evt.session_id);
            }
        }

        // 2. Process control channel commands
        control.process_pending_commands();

        // 3. Tick ambient faults + duration tracking
        fault_registry.on_tick(tick);

        // 4. Tick all zones (pre_tick_hooks fire execute_pre_tick_faults)
        zone_manager.tick_all(tick);
    });

    // -----------------------------------------------------------------------
    // Banner
    // -----------------------------------------------------------------------
    std::cout << "wow-server-sim v0.1.0\n"
              << "WoW Server Simulator — reliability engineering demo\n"
              << "  Game server:     port " << game_server.port() << "\n"
              << "  Control channel: port " << control.port() << "\n"
              << "  Tick rate:       20 Hz (50ms)\n"
              << "  Zones:           Elwynn Forest, Westfall\n"
              << "  Faults:          F1-F8 registered\n"
              << "  Telemetry:       telemetry.jsonl\n"
              << "Press Ctrl+C to stop.\n"
              << std::flush;

    // -----------------------------------------------------------------------
    // 10. Run — blocks until stop() is called from tick callback
    // -----------------------------------------------------------------------
    game_loop.run();

    // -----------------------------------------------------------------------
    // 11. Shutdown — orderly teardown
    // -----------------------------------------------------------------------
    wow::Logger::instance().event("server", "Shutting down...");

    game_server.stop();
    control.stop();
    fault_registry.deactivate_all();

    wow::Logger::instance().event("server", "Server stopped", {
        {"total_ticks", game_loop.tick_count()},
    });

    wow::Logger::reset();

    std::cout << "Server stopped after " << game_loop.tick_count()
              << " ticks.\n";

    return EXIT_SUCCESS;
}
