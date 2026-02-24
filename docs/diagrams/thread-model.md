# Thread Model

> Part of the [Architecture Documentation](../ARCHITECTURE.md).

```mermaid
graph LR
    subgraph NetThread["Network Thread (Asio io_context)"]
        Accept["TCP Accept :8080"]
        Read["Async Read/Write"]
        SessEmit["Push SessionNotification"]
        EvtEmit["Push GameEvent"]
    end

    subgraph CtrlThread["Control Channel Thread (Asio io_context)"]
        CtrlAccept["TCP Accept :8081"]
        Parse["Parse JSON Command"]
        CmdEmit["Push ControlCommand"]
    end

    SEQ{{"SessionEventQueue<br/>(mutex + swap drain)"}}
    EQ{{"EventQueue<br/>(mutex + swap drain)"}}
    CQ{{"CommandQueue<br/>(mutex + swap drain)"}}

    SessEmit --> SEQ
    EvtEmit --> EQ
    CmdEmit --> CQ

    SEQ --> DrainSess
    EQ --> RouteEvt
    CQ --> DrainCmd

    subgraph GameThread["Game Loop — Main Thread (20 Hz)"]
        direction TB
        DrainSess["1. Drain session events<br/>assign/remove players"]
        DrainCmd["2. Process control commands<br/>activate/deactivate faults"]
        TickFaults["3. Tick ambient faults<br/>FaultRegistry.on_tick()"]
        TickZones["4. Tick all zones<br/>ZoneManager.tick_all()"]

        DrainSess --> DrainCmd
        DrainCmd --> TickFaults
        TickFaults --> TickZones
    end

    RouteEvt["ZoneManager.route_events()"]
    RouteEvt --> TickZones

    style SEQ fill:#f5a623,color:#000
    style EQ fill:#f5a623,color:#000
    style CQ fill:#f5a623,color:#000
    style NetThread fill:#e8f4fd,color:#000
    style CtrlThread fill:#e8f4fd,color:#000
    style GameThread fill:#fef9e7,color:#000
```

The MVP uses three threads with strict ownership boundaries. The **network thread** and **control channel thread** each run their own Asio `io_context` and only produce data — they never touch game state directly. Three thread-safe queues (all using the same mutex + swap-drain pattern) bridge data to the **game loop on the main thread**, which owns all mutable state. The game loop processes queues in a fixed order each tick: session events first (player join/leave), then control commands (fault activation), then ambient fault ticks, and finally zone ticks. This ordering ensures faults take effect on the same tick they are activated.
