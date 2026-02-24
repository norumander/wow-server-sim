#include "server/session_event_queue.h"

namespace wow {

void SessionEventQueue::push(SessionNotification event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

std::vector<SessionNotification> SessionEventQueue::drain()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionNotification> result;
    result.swap(events_);
    return result;
}

size_t SessionEventQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

bool SessionEventQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}

}  // namespace wow
