#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "server/session_event_queue.h"

// ===========================================================================
// SessionEventQueue Tests (4 tests)
// ===========================================================================

TEST(SessionEventQueue, Construction_EmptyByDefault)
{
    wow::SessionEventQueue queue;
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());

    auto drained = queue.drain();
    EXPECT_TRUE(drained.empty());
}

TEST(SessionEventQueue, PushDrain_RoundTrip)
{
    wow::SessionEventQueue queue;

    queue.push({wow::SessionEventType::CONNECTED, 42});
    queue.push({wow::SessionEventType::DISCONNECTED, 99});

    auto events = queue.drain();
    ASSERT_EQ(events.size(), 2u);

    EXPECT_EQ(events[0].type, wow::SessionEventType::CONNECTED);
    EXPECT_EQ(events[0].session_id, 42u);

    EXPECT_EQ(events[1].type, wow::SessionEventType::DISCONNECTED);
    EXPECT_EQ(events[1].session_id, 99u);
}

TEST(SessionEventQueue, Drain_ClearsQueue)
{
    wow::SessionEventQueue queue;

    queue.push({wow::SessionEventType::CONNECTED, 1});
    queue.push({wow::SessionEventType::CONNECTED, 2});

    auto first = queue.drain();
    EXPECT_EQ(first.size(), 2u);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);

    auto second = queue.drain();
    EXPECT_TRUE(second.empty());
}

TEST(SessionEventQueue, ConcurrentPush_SingleDrain)
{
    wow::SessionEventQueue queue;

    constexpr int kThreads = 4;
    constexpr int kEventsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&queue, t, kEventsPerThread] {
            for (int i = 0; i < kEventsPerThread; ++i) {
                uint64_t id = static_cast<uint64_t>(t * kEventsPerThread + i);
                queue.push({wow::SessionEventType::CONNECTED, id});
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    auto events = queue.drain();
    EXPECT_EQ(events.size(), static_cast<size_t>(kThreads * kEventsPerThread));
    EXPECT_TRUE(queue.empty());
}
