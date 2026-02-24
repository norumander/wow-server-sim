# Tick Pipeline (Per Zone)

> Part of the [Architecture Documentation](../ARCHITECTURE.md).

```mermaid
graph TD
    Start(["Zone.tick(current_tick)"])
    PreTick["FaultPreTickPhase<br/>pre_tick_hook()"]
    Drain["DrainIntakePhase<br/>event_queue_.drain()"]
    Move["MovementPhase<br/>MovementProcessor::process()<br/>— position updates, set moved_this_tick"]
    Spell["SpellCastPhase<br/>SpellCastProcessor::process()<br/>— cancel / interrupt / advance / start / clear"]
    Combat["CombatPhase<br/>CombatProcessor::process()<br/>— damage, threat, NPC auto-attack, cleanup"]
    PostTick["FaultPostTickPhase<br/>post_tick_hook()"]
    Recovery["StateRecoveryPhase<br/>CRASHED → DEGRADED → ACTIVE"]
    Telemetry["TelemetryEmitPhase<br/>emit zone tick completed metric"]
    End(["Return ZoneTickResult"])

    Start --> PreTick
    PreTick --> Drain
    Drain --> Move
    Move --> Spell
    Spell --> Combat
    Combat --> PostTick
    PostTick --> Recovery
    Recovery --> Telemetry
    Telemetry --> End

    ExGuard["Exception Guard<br/>try/catch wraps entire pipeline<br/>→ zone state = CRASHED on exception"]

    ExGuard -. "protects" .-> PreTick
    ExGuard -. "protects" .-> Drain
    ExGuard -. "protects" .-> Move
    ExGuard -. "protects" .-> Spell
    ExGuard -. "protects" .-> Combat
    ExGuard -. "protects" .-> PostTick

    style PreTick fill:#e67e22,color:#fff
    style PostTick fill:#e67e22,color:#fff
    style ExGuard fill:#e74c3c,color:#fff
    style Move fill:#3498db,color:#fff
    style Spell fill:#3498db,color:#fff
    style Combat fill:#3498db,color:#fff
    style Recovery fill:#2ecc71,color:#fff
    style Telemetry fill:#9b59b6,color:#fff
```

Each zone executes an 8-phase pipeline on every tick. The two **fault injection points** (orange) — `FaultPreTickPhase` and `FaultPostTickPhase` — are hooks set by the FaultRegistry, allowing faults like latency spikes (F1) and event floods (F3) to fire at controlled moments. The three **game processing phases** (blue) run in a fixed order: movement first (so `moved_this_tick` is set before spell processing checks it), then spell casts, then combat. An **exception guard** wraps the entire pipeline in `try/catch` — if any phase throws, the zone transitions to CRASHED without affecting other zones. The **state recovery phase** (green) promotes zones back toward ACTIVE on successive successful ticks.
