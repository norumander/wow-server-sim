# System Overview

> Part of the [Architecture Documentation](../ARCHITECTURE.md).

```mermaid
graph TD
    subgraph Docker["Docker Container"]
        subgraph Server["C++ Game Server"]
            NT["Network Thread<br/>TCP :8080"]
            CC["Control Channel Thread<br/>TCP :8081"]
            GL["Game Loop<br/>(main thread, 20 Hz)"]

            NT -- "SessionEventQueue" --> GL
            NT -- "EventQueue<br/>(intake)" --> ZM
            CC -- "CommandQueue" --> GL

            GL --> FR["FaultRegistry"]
            GL --> ZM

            subgraph ZM["ZoneManager"]
                Z1["Zone 1<br/>Elwynn Forest"]
                Z2["Zone 2<br/>Westfall"]
            end

            ZM --> TL["Telemetry Emitter"]
            TL --> LF[("telemetry.jsonl<br/>(durable file)")]
        end
    end

    CLI["wowsim CLI<br/>(Python)"] -- "TCP :8081" --> CC
    MC["Mock Clients<br/>(Python)"] -- "TCP :8080" --> NT
    DASH["Dashboard TUI<br/>(Python)"] -- "TCP :8081" --> CC
    DASH -- "reads JSONL" --> LF
    CLI -- "reads JSONL" --> LF

    style NT fill:#4a90d9,color:#fff
    style CC fill:#4a90d9,color:#fff
    style GL fill:#e8a838,color:#fff
    style FR fill:#d94a4a,color:#fff
    style Z1 fill:#5cb85c,color:#fff
    style Z2 fill:#5cb85c,color:#fff
    style TL fill:#9b59b6,color:#fff
```

The system runs inside a Docker container with the C++ game server as its core. Three threads handle distinct responsibilities: the **network thread** accepts game client connections on port 8080, the **control channel thread** accepts operator commands on port 8081, and the **main thread** runs the 20 Hz game loop. Three thread-safe queues (SessionEventQueue, EventQueue, CommandQueue) bridge the network threads to the game loop, which owns all mutable game state including the FaultRegistry and ZoneManager. Python tooling connects externally via TCP and reads the durable JSONL telemetry log file for observability.
