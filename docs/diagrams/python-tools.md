# Python Tool Composition

> Part of the [Architecture Documentation](../ARCHITECTURE.md).

```mermaid
graph TD
    CLI["wowsim CLI<br/>(Click entrypoint)"]

    CLI --> LP["log_parser<br/>parse, filter, anomalies"]
    CLI --> FT["fault_trigger<br/>ControlClient, activate/deactivate"]
    CLI --> HC["health_check<br/>tick health, zone health, status"]
    CLI --> MC["mock_client<br/>async traffic generation"]
    CLI --> DASH["dashboard<br/>Textual TUI"]
    CLI --> PIPE["pipeline<br/>staged deploy lifecycle"]
    CLI --> BENCH["benchmark<br/>scaling tests, percentiles"]

    subgraph Composers["Composer Modules (reuse other tools)"]
        DASH
        PIPE
        BENCH
    end

    subgraph Core["Core Modules (direct I/O)"]
        LP
        FT
        HC
        MC
    end

    DASH --> HC
    DASH --> LP
    DASH --> FT

    PIPE --> HC
    PIPE --> FT

    BENCH --> MC
    BENCH --> HC

    HC --> LP
    HC --> FT

    Models["models<br/>(Pydantic v2)<br/>TelemetryEntry, Anomaly,<br/>FaultInfo, HealthReport,<br/>PipelineResult, BenchmarkResult, ..."]

    LP --> Models
    FT --> Models
    HC --> Models
    MC --> Models
    DASH --> Models
    PIPE --> Models
    BENCH --> Models

    style CLI fill:#2c3e50,color:#fff
    style Models fill:#8e44ad,color:#fff
    style Composers fill:#fef9e7,color:#000
    style Core fill:#e8f4fd,color:#000
    style DASH fill:#e67e22,color:#fff
    style PIPE fill:#e67e22,color:#fff
    style BENCH fill:#e67e22,color:#fff
```

The Python tooling follows a layered composition pattern. Four **core modules** handle direct I/O: `log_parser` reads JSONL files, `fault_trigger` talks to the C++ control channel via TCP, `mock_client` generates async traffic, and `health_check` aggregates data from log_parser and fault_trigger. Three **composer modules** (orange) build higher-level workflows by reusing core modules: `dashboard` combines health_check + log_parser + fault_trigger into a live TUI, `pipeline` orchestrates health_check + fault_trigger into a staged deployment lifecycle, and `benchmark` composes mock_client + health_check into automated scaling tests. All modules share a common `models` layer (Pydantic v2) ensuring type-safe data exchange across the toolkit.
