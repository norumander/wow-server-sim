#include <cstdlib>
#include <iostream>

#include "server/telemetry/logger.h"

/// Entry point for the WoW Server Simulator.
///
/// Initializes telemetry logging, prints server info, and exits cleanly.
/// Game loop, networking, and subsystems are added via TDD.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    wow::LoggerConfig log_config;
    log_config.stdout_enabled = true;
    wow::Logger::initialize(log_config);

    wow::Logger::instance().event("server", "Server starting",
                                  {{"version", "0.1.0"},
                                   {"tick_rate_hz", 20}});

    std::cout << "wow-server-sim v0.1.0\n"
              << "WoW Server Simulator — reliability engineering demo\n"
              << "Tick rate: 20 Hz (50ms)\n"
              << "Status: scaffolding only — no subsystems loaded\n";

    wow::Logger::instance().event("server", "Server shutting down");
    wow::Logger::reset();

    return EXIT_SUCCESS;
}
