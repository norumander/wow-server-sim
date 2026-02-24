#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace wow {

/// Type of session lifecycle event pushed from the network thread.
enum class SessionEventType { CONNECTED, DISCONNECTED };

/// Notification that a session has connected or disconnected.
///
/// Pushed by GameServer's network thread, drained by the game loop at
/// tick start. Lightweight value type — no heap allocation, trivially copyable.
struct SessionNotification {
    SessionEventType type;
    uint64_t session_id;
};

/// Thread-safe queue bridging session lifecycle events from the network
/// thread to the game thread.
///
/// Follows the same mutex + swap-drain pattern as EventQueue and CommandQueue
/// (see ADR-002, ADR-017). GameServer pushes CONNECTED/DISCONNECTED events
/// from the network thread; the game loop drains at tick start to assign
/// or remove sessions from zones.
class SessionEventQueue {
public:
    /// Push a session event (called from the network thread).
    void push(SessionNotification event);

    /// Drain all queued events, returning them and clearing the queue.
    /// Uses swap for O(1) bulk transfer (called from the game thread).
    std::vector<SessionNotification> drain();

    /// Current queue depth (thread-safe).
    size_t size() const;

    /// Whether the queue is empty (thread-safe).
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::vector<SessionNotification> events_;
};

}  // namespace wow
