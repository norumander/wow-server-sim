#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "control/control_channel.h"
#include "server/fault/injector.h"
#include "server/fault/scenarios.h"
#include "server/telemetry/logger.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

// =============================================================================
// Group A: CommandQueue (3 tests)
// =============================================================================

TEST(CommandQueue, EmptyDrainReturnsEmpty)
{
    wow::CommandQueue queue;
    auto commands = queue.drain();
    EXPECT_TRUE(commands.empty());
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST(CommandQueue, PushAndDrain)
{
    wow::CommandQueue queue;

    queue.push({json{{"command", "list"}}, nullptr});
    queue.push({json{{"command", "status"}}, nullptr});
    queue.push({json{{"command", "activate"}}, nullptr});

    EXPECT_EQ(queue.size(), 3u);
    EXPECT_FALSE(queue.empty());

    auto commands = queue.drain();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].request["command"], "list");
    EXPECT_EQ(commands[1].request["command"], "status");
    EXPECT_EQ(commands[2].request["command"], "activate");

    // Queue should be empty after drain
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST(CommandQueue, DrainClearsQueue)
{
    wow::CommandQueue queue;

    queue.push({json{{"command", "a"}}, nullptr});
    queue.push({json{{"command", "b"}}, nullptr});

    auto first = queue.drain();
    ASSERT_EQ(first.size(), 2u);

    // Push one more after draining
    queue.push({json{{"command", "c"}}, nullptr});

    auto second = queue.drain();
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].request["command"], "c");
}
