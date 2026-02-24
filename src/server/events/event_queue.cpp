#include "server/events/event_queue.h"

namespace wow {

// ---------------------------------------------------------------------------
// EventQueue — stub (implemented in Commit 2)
// ---------------------------------------------------------------------------

void EventQueue::push(std::unique_ptr<GameEvent> /*event*/)
{
    // stub
}

std::vector<std::unique_ptr<GameEvent>> EventQueue::drain()
{
    return {};
}

size_t EventQueue::size() const
{
    return 0;
}

bool EventQueue::empty() const
{
    return true;
}

}  // namespace wow
