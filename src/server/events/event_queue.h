#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "server/events/event.h"

namespace wow {

/// Thread-safe event queue for producer/consumer between network and game threads.
///
/// Network thread pushes events; game thread drains all events at tick start.
/// Lock granularity: one mutex, push() and drain() each hold it briefly.
class EventQueue {
public:
    /// Push an event (thread-safe, called from network thread).
    void push(std::unique_ptr<GameEvent> event);

    /// Drain all queued events, returning them and clearing the queue.
    /// Thread-safe, called from game thread at tick start.
    std::vector<std::unique_ptr<GameEvent>> drain();

    /// Current queue depth (thread-safe, for telemetry).
    size_t size() const;

    /// Whether the queue is empty (thread-safe).
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<GameEvent>> events_;
};

}  // namespace wow
