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

// =============================================================================
// Test fixture — provides helpers for connecting test clients, waiting for
// async conditions, and capturing telemetry. All tests use port 0 (OS-
// assigned) to avoid CI port conflicts. Pre-registers LatencySpikeFault
// and MemoryPressureFault for command dispatch tests.
// =============================================================================

class ControlChannelTest : public ::testing::Test {
protected:
    std::ostringstream sink_;
    wow::FaultRegistry registry_;

    void SetUp() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
        wow::LoggerConfig config;
        config.custom_sink = &sink_;
        wow::Logger::initialize(config);

        registry_.register_fault(std::make_unique<wow::LatencySpikeFault>());
        registry_.register_fault(std::make_unique<wow::MemoryPressureFault>());
    }

    void TearDown() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    /// Connect a synchronous TCP client to the given port on localhost.
    asio::ip::tcp::socket connect_client(asio::io_context& ctx, uint16_t port)
    {
        asio::ip::tcp::socket sock(ctx);
        asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), port);
        sock.connect(ep);
        return sock;
    }

    /// Poll a predicate at 10ms intervals, failing after timeout.
    bool wait_for(std::function<bool()> predicate,
                  std::chrono::milliseconds timeout = 500ms)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return true;
            std::this_thread::sleep_for(10ms);
        }
        return predicate();
    }
};

// =============================================================================
// Group B: Construction & Lifecycle (4 tests)
// =============================================================================

TEST_F(ControlChannelTest, ControlChannel_NotRunningAfterConstruction)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    EXPECT_FALSE(channel.is_running());
}

TEST_F(ControlChannelTest, ControlChannel_StartSetsRunningAndPort)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();
    EXPECT_TRUE(channel.is_running());
    EXPECT_GT(channel.port(), 0u);
    channel.stop();
}

TEST_F(ControlChannelTest, ControlChannel_StopSetsNotRunning)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();
    channel.stop();
    EXPECT_FALSE(channel.is_running());
}

TEST_F(ControlChannelTest, ControlChannel_DestructorStops)
{
    wow::ControlChannelConfig cfg{0};
    {
        wow::ControlChannel channel(registry_, cfg);
        channel.start();
        EXPECT_TRUE(channel.is_running());
    } // Destructor should call stop() — no hang or crash
}

// =============================================================================
// Group C: Connection Handling (3 tests)
// =============================================================================

TEST_F(ControlChannelTest, ControlChannel_AcceptSingleClient)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());

    EXPECT_TRUE(wait_for([&] { return channel.client_count() == 1; }));
    EXPECT_EQ(channel.client_count(), 1u);

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, ControlChannel_AcceptMultipleClients)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto s1 = connect_client(client_ctx, channel.port());
    auto s2 = connect_client(client_ctx, channel.port());

    EXPECT_TRUE(wait_for([&] { return channel.client_count() == 2; }));
    EXPECT_EQ(channel.client_count(), 2u);

    s1.close();
    s2.close();
    channel.stop();
}

TEST_F(ControlChannelTest, ControlChannel_DisconnectReducesCount)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    sock.close();
    EXPECT_TRUE(wait_for([&] { return channel.client_count() == 0; }));
    EXPECT_EQ(channel.client_count(), 0u);

    channel.stop();
}
