#include <cstdlib>
#include <iostream>

/// Entry point for the WoW Server Simulator.
///
/// Currently prints server info and exits cleanly.
/// Game loop, networking, and subsystems are added via TDD.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "wow-server-sim v0.1.0\n"
              << "WoW Server Simulator — reliability engineering demo\n"
              << "Tick rate: 20 Hz (50ms)\n"
              << "Status: scaffolding only — no subsystems loaded\n";

    return EXIT_SUCCESS;
}
