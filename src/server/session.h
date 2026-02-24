#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace wow {

/// States in the player session lifecycle.
///
/// Models a WoW player connection from initial TCP accept through
/// authentication, gameplay, optional zone transfer, and cleanup.
enum class SessionState {
    CONNECTING,     ///< TCP connection accepted, awaiting auth handshake
    AUTHENTICATING, ///< Auth handshake in progress
    IN_WORLD,       ///< Fully authenticated and active in a zone
    TRANSFERRING,   ///< Moving between zones (e.g. instance portal)
    DISCONNECTING,  ///< Graceful or abrupt disconnect, awaiting cleanup
    DESTROYED,      ///< Terminal state — session resources released
};

/// Events that trigger state transitions in the session state machine.
enum class SessionEvent {
    AUTHENTICATE_SUCCESS, ///< Auth handshake completed successfully
    ENTER_WORLD,          ///< Player placed into a zone
    DISCONNECT,           ///< Connection lost or client initiated disconnect
    BEGIN_TRANSFER,       ///< Zone transfer initiated (e.g. entering instance)
    TRANSFER_COMPLETE,    ///< Arrived in destination zone
    RECONNECT,            ///< Client reconnected within grace window
    TIMEOUT,              ///< Reconnection grace period expired
};

/// Convert a SessionState enum value to its string representation.
std::string_view session_state_to_string(SessionState state);

/// Convert a SessionEvent enum value to its string representation.
std::string_view session_event_to_string(SessionEvent event);

/// A single entry in the session transition table.
struct SessionTransition {
    SessionState from;
    SessionEvent event;
    SessionState to;
};

/// Represents a player's authenticated connection with state machine lifecycle.
///
/// Each session tracks a unique ID (auto-assigned at construction) and a
/// current state. All state changes go through the transition() method,
/// which validates against a transition table and emits telemetry.
///
/// Sessions are non-copyable but movable, enabling ownership transfer
/// (e.g. from accept queue to zone).
class Session {
public:
    /// Construct a new session in CONNECTING state with a unique ID.
    Session();

    /// Return this session's unique identifier.
    uint64_t session_id() const;

    /// Return the current state.
    SessionState state() const;

    /// Attempt a state transition triggered by the given event.
    ///
    /// Looks up {current_state, event} in the transition table.
    /// On match: updates state, emits telemetry event, returns true.
    /// On miss: state unchanged, emits telemetry error, returns false.
    bool transition(SessionEvent event);

    // Non-copyable.
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Movable.
    Session(Session&&) = default;
    Session& operator=(Session&&) = default;

private:
    uint64_t session_id_;
    SessionState state_ = SessionState::CONNECTING;

    /// Monotonically increasing ID generator. Starts at 1.
    static std::atomic<uint64_t> next_id_;
};

}  // namespace wow
