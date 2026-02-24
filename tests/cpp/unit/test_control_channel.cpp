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

    /// Send a JSON command over a connected socket, trigger game-thread
    /// processing, and read back the JSON response. Uses async read with
    /// deadline timer to avoid blocking forever when server doesn't respond.
    json send_command(asio::ip::tcp::socket& sock,
                      const json& request,
                      wow::ControlChannel& channel)
    {
        // Write the JSON line to the socket.
        std::string line = request.dump() + "\n";
        asio::write(sock, asio::buffer(line));

        // Give the network thread time to receive and queue the command.
        std::this_thread::sleep_for(50ms);

        // Process pending commands on the "game thread" (this thread).
        channel.process_pending_commands();

        // Give the network thread time to send the response.
        std::this_thread::sleep_for(50ms);

        // Read the response line with timeout.
        auto response_buf = std::make_shared<asio::streambuf>();
        asio::io_context read_ctx;
        asio::ip::tcp::socket::native_handle_type native = sock.native_handle();

        std::string response_line;
        bool read_complete = false;
        asio::error_code read_ec;

        // Use a temporary wrapper socket on a separate io_context for
        // async_read_until + deadline timer.
        // Simpler approach: poll with available() + short sleeps.
        auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (sock.available() > 0) {
                asio::read_until(sock, *response_buf, '\n');
                read_complete = true;
                break;
            }
            std::this_thread::sleep_for(10ms);
        }

        if (!read_complete) {
            ADD_FAILURE() << "Timed out waiting for response from control channel";
            return json{{"error", "timeout"}};
        }

        std::istream stream(response_buf.get());
        std::getline(stream, response_line);

        // Remove trailing \r if present.
        if (!response_line.empty() && response_line.back() == '\r') {
            response_line.pop_back();
        }

        return json::parse(response_line);
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

// =============================================================================
// Group D: Activate Command (3 tests)
// =============================================================================

TEST_F(ControlChannelTest, Activate_SucceedsForRegisteredFault)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{{"command", "activate"}, {"fault_id", "latency-spike"}}, channel);
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["command"], "activate");
    EXPECT_EQ(response["fault_id"], "latency-spike");
    EXPECT_TRUE(registry_.is_active("latency-spike"));

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, Activate_FailsForUnknownFault)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{{"command", "activate"}, {"fault_id", "nonexistent"}}, channel);
    EXPECT_FALSE(response["success"].get<bool>());
    EXPECT_TRUE(response.contains("error"));

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, Activate_WithFullConfig)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{
        {"command", "activate"},
        {"fault_id", "latency-spike"},
        {"params", {{"delay_ms", 100}}},
        {"target_zone_id", 2},
        {"duration_ticks", 50}
    }, channel);

    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_TRUE(registry_.is_active("latency-spike"));

    sock.close();
    channel.stop();
}

// =============================================================================
// Group E: Deactivate Commands (3 tests)
// =============================================================================

TEST_F(ControlChannelTest, Deactivate_SucceedsForActiveFault)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    // First activate
    send_command(sock, json{{"command", "activate"}, {"fault_id", "latency-spike"}}, channel);
    ASSERT_TRUE(registry_.is_active("latency-spike"));

    // Then deactivate
    auto response = send_command(sock, json{{"command", "deactivate"}, {"fault_id", "latency-spike"}}, channel);
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["command"], "deactivate");
    EXPECT_FALSE(registry_.is_active("latency-spike"));

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, Deactivate_FailsForUnknownFault)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{{"command", "deactivate"}, {"fault_id", "nonexistent"}}, channel);
    EXPECT_FALSE(response["success"].get<bool>());
    EXPECT_TRUE(response.contains("error"));

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, DeactivateAll_DeactivatesAllFaults)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    // Activate both faults
    send_command(sock, json{{"command", "activate"}, {"fault_id", "latency-spike"}}, channel);
    send_command(sock, json{{"command", "activate"}, {"fault_id", "memory-pressure"}, {"params", {{"megabytes", 1}}}}, channel);
    ASSERT_EQ(registry_.active_count(), 2u);

    // Deactivate all
    auto response = send_command(sock, json{{"command", "deactivate_all"}}, channel);
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["command"], "deactivate_all");
    EXPECT_EQ(registry_.active_count(), 0u);

    sock.close();
    channel.stop();
}

// =============================================================================
// Group F: Status & List Commands (3 tests)
// =============================================================================

TEST_F(ControlChannelTest, Status_ReturnsActiveFaultInfo)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    // Activate latency-spike first
    send_command(sock, json{{"command", "activate"}, {"fault_id", "latency-spike"}}, channel);

    auto response = send_command(sock, json{{"command", "status"}, {"fault_id", "latency-spike"}}, channel);
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["command"], "status");
    ASSERT_TRUE(response.contains("status"));
    EXPECT_EQ(response["status"]["id"], "latency-spike");
    EXPECT_EQ(response["status"]["mode"], "tick_scoped");
    EXPECT_TRUE(response["status"]["active"].get<bool>());

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, Status_FailsForUnknownFault)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{{"command", "status"}, {"fault_id", "nonexistent"}}, channel);
    EXPECT_FALSE(response["success"].get<bool>());
    EXPECT_TRUE(response.contains("error"));

    sock.close();
    channel.stop();
}

TEST_F(ControlChannelTest, List_ReturnsAllRegisteredFaults)
{
    wow::ControlChannelConfig cfg{0};
    wow::ControlChannel channel(registry_, cfg);
    channel.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, channel.port());
    ASSERT_TRUE(wait_for([&] { return channel.client_count() == 1; }));

    auto response = send_command(sock, json{{"command", "list"}}, channel);
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_EQ(response["command"], "list");
    ASSERT_TRUE(response.contains("faults"));
    ASSERT_EQ(response["faults"].size(), 2u);

    // Verify each fault has id, mode, active fields
    for (const auto& fault : response["faults"]) {
        EXPECT_TRUE(fault.contains("id"));
        EXPECT_TRUE(fault.contains("mode"));
        EXPECT_TRUE(fault.contains("active"));
    }

    sock.close();
    channel.stop();
}
