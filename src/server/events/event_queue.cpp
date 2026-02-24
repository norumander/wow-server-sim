#include "server/events/event_queue.h"

#include <utility>

namespace wow {

void EventQueue::push(std::unique_ptr<GameEvent> event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
}

std::vector<std::unique_ptr<GameEvent>> EventQueue::drain()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::unique_ptr<GameEvent>> result;
    result.swap(events_);
    return result;
}

size_t EventQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

bool EventQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty();
}

}  // namespace wow
