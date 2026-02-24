# Session State Machine

> Part of the [Architecture Documentation](../ARCHITECTURE.md).

```mermaid
stateDiagram-v2
    [*] --> CONNECTING

    CONNECTING --> AUTHENTICATING : AUTHENTICATE_SUCCESS
    CONNECTING --> DESTROYED : DISCONNECT

    AUTHENTICATING --> IN_WORLD : ENTER_WORLD
    AUTHENTICATING --> DISCONNECTING : DISCONNECT

    IN_WORLD --> DISCONNECTING : DISCONNECT
    IN_WORLD --> TRANSFERRING : BEGIN_TRANSFER

    TRANSFERRING --> IN_WORLD : TRANSFER_COMPLETE
    TRANSFERRING --> DISCONNECTING : DISCONNECT

    DISCONNECTING --> AUTHENTICATING : RECONNECT
    DISCONNECTING --> DESTROYED : TIMEOUT

    DESTROYED --> [*]

    note right of CONNECTING
        Auto-assigned unique
        monotonic session ID
    end note

    note right of IN_WORLD
        Player assigned to a Zone,
        entity participates in
        tick pipeline
    end note

    note right of DISCONNECTING
        Reconnection window —
        RECONNECT returns to
        AUTHENTICATING
    end note

    note right of DESTROYED
        Terminal state,
        session resources freed
    end note
```

The session lifecycle tracks a player's connection through 6 states and 10 transitions, implemented as a `constexpr` transition table in `wow::Session`. A new connection starts in **CONNECTING** and progresses through **AUTHENTICATING** into **IN_WORLD** where the player's entity participates in zone tick processing. The **TRANSFERRING** state supports zone transfers (e.g., moving between Elwynn Forest and Westfall). **DISCONNECTING** provides a reconnection window — the `RECONNECT` event returns the session to `AUTHENTICATING` rather than requiring a fresh connection. The `TIMEOUT` event in DISCONNECTING leads to the terminal **DESTROYED** state where all session resources are freed.
