#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "server/game_server.h"
#include "server/session.h"
#include "server/telemetry/logger.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

// ===========================================================================
// Test fixture — provides helpers for connecting test clients, waiting for
// async conditions, and capturing telemetry. All tests use port 0 (OS-
// assigned) to avoid CI port conflicts.
// ===========================================================================

class GameServerTest : public ::testing::Test {
protected:
    std::ostringstream sink_;

    void SetUp() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
        wow::LoggerConfig config;
        config.custom_sink = &sink_;
        wow::Logger::initialize(config);
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

    /// Poll a predicate at 10ms intervals, failing after timeout_ms.
    bool wait_for(std::function<bool()> predicate,
                  std::chrono::milliseconds timeout = 500ms)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return true;
            std::this_thread::sleep_for(10ms);
        }
        return predicate(); // One final check
    }

    /// Parse all JSON lines from the telemetry sink.
    std::vector<json> parse_all_lines()
    {
        std::vector<json> entries;
        std::istringstream stream(sink_.str());
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                entries.push_back(json::parse(line));
            }
        }
        return entries;
    }

    /// Filter telemetry entries by type and component.
    std::vector<json> filter_entries(const std::string& type,
                                     const std::string& component)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            if (entry.value("type", "") == type &&
                entry.value("component", "") == component) {
                result.push_back(entry);
            }
        }
        return result;
    }

    /// Filter telemetry entries by message substring.
    std::vector<json> filter_by_message(const std::string& substr)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            auto msg = entry.value("message", "");
            if (msg.find(substr) != std::string::npos) {
                result.push_back(entry);
            }
        }
        return result;
    }
};

// ===========================================================================
// Group A: Construction (3 tests)
// ===========================================================================

TEST_F(GameServerTest, GameServer_IsNotRunningAfterConstruction)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    EXPECT_FALSE(server.is_running());
}

TEST_F(GameServerTest, GameServer_PortIsZeroBeforeStart)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    EXPECT_EQ(server.port(), 0u);
}

TEST_F(GameServerTest, GameServer_ConnectionCountIsZeroAfterConstruction)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    EXPECT_EQ(server.connection_count(), 0u);
}

// ===========================================================================
// Group B: Start/Stop Lifecycle (5 tests)
// ===========================================================================

TEST_F(GameServerTest, Lifecycle_StartSetsRunningTrue)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    EXPECT_TRUE(server.is_running());
    server.stop();
}

TEST_F(GameServerTest, Lifecycle_StartAssignsNonZeroPort)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    EXPECT_GT(server.port(), 0u);
    server.stop();
}

TEST_F(GameServerTest, Lifecycle_StopSetsRunningFalse)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST_F(GameServerTest, Lifecycle_StopIsIdempotent)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    server.stop();
    server.stop(); // Second call should not crash or throw
    EXPECT_FALSE(server.is_running());
}

TEST_F(GameServerTest, Lifecycle_DestructorStopsRunningServer)
{
    wow::GameServerConfig cfg{0};
    {
        wow::GameServer server(cfg);
        server.start();
        EXPECT_TRUE(server.is_running());
    } // Destructor should call stop() — no hang or crash
}

// ===========================================================================
// Group C: Connection Acceptance (4 tests)
// ===========================================================================

TEST_F(GameServerTest, Accept_SingleConnection)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());

    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 1; }));
    EXPECT_EQ(server.connection_count(), 1u);

    sock.close();
    server.stop();
}

TEST_F(GameServerTest, Accept_MultipleConnections)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto s1 = connect_client(client_ctx, server.port());
    auto s2 = connect_client(client_ctx, server.port());
    auto s3 = connect_client(client_ctx, server.port());

    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 3; }));
    EXPECT_EQ(server.connection_count(), 3u);

    s1.close();
    s2.close();
    s3.close();
    server.stop();
}

TEST_F(GameServerTest, Accept_CreatesSessionInConnectingState)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());

    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    // Verify telemetry records a "Connection accepted" event
    auto events = filter_by_message("Connection accepted");
    ASSERT_GE(events.size(), 1u);

    sock.close();
    server.stop();
}

TEST_F(GameServerTest, Accept_WorksOnConfiguredPort)
{
    // Find a free port by binding temporarily
    asio::io_context probe_ctx;
    asio::ip::tcp::acceptor probe(probe_ctx,
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    uint16_t free_port = probe.local_endpoint().port();
    probe.close();

    wow::GameServerConfig cfg{free_port};
    wow::GameServer server(cfg);
    server.start();
    EXPECT_EQ(server.port(), free_port);

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, free_port);
    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    sock.close();
    server.stop();
}

// ===========================================================================
// Group D: Disconnect Handling (4 tests)
// ===========================================================================

TEST_F(GameServerTest, Disconnect_ClientCloseReducesCount)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    sock.close();
    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 0; }));
    EXPECT_EQ(server.connection_count(), 0u);

    server.stop();
}

TEST_F(GameServerTest, Disconnect_TransitionsSessionViaDisconnectEvent)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    sock.close();
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 0; }));

    // Session should have transitioned CONNECTING -> DESTROYED via DISCONNECT
    auto events = filter_by_message("Client disconnected");
    EXPECT_GE(events.size(), 1u);

    server.stop();
}

TEST_F(GameServerTest, Disconnect_OneOfMultipleClients)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto s1 = connect_client(client_ctx, server.port());
    auto s2 = connect_client(client_ctx, server.port());
    auto s3 = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 3; }));

    s2.close();
    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 2; }));
    EXPECT_EQ(server.connection_count(), 2u);

    s1.close();
    s3.close();
    server.stop();
}

TEST_F(GameServerTest, Disconnect_ServerStopClosesAll)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto s1 = connect_client(client_ctx, server.port());
    auto s2 = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 2; }));

    server.stop();
    EXPECT_EQ(server.connection_count(), 0u);

    s1.close();
    s2.close();
}

// ===========================================================================
// Group E: Telemetry (4 tests)
// ===========================================================================

TEST_F(GameServerTest, Telemetry_EmitsServerStartedEvent)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    auto events = filter_by_message("Server started");
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0]["component"].get<std::string>(), "game_server");
    EXPECT_TRUE(events[0]["data"].contains("port"));

    server.stop();
}

TEST_F(GameServerTest, Telemetry_EmitsServerStoppedEvent)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    server.stop();

    auto events = filter_by_message("Server stopped");
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0]["component"].get<std::string>(), "game_server");
}

TEST_F(GameServerTest, Telemetry_EmitsConnectionAccepted)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    auto events = filter_by_message("Connection accepted");
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0]["component"].get<std::string>(), "game_server");
    EXPECT_TRUE(events[0]["data"].contains("session_id"));
    EXPECT_TRUE(events[0]["data"].contains("remote_endpoint"));

    sock.close();
    server.stop();
}

TEST_F(GameServerTest, Telemetry_EmitsClientDisconnected)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    auto sock = connect_client(client_ctx, server.port());
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 1; }));

    sock.close();
    ASSERT_TRUE(wait_for([&] { return server.connection_count() == 0; }));

    auto events = filter_by_message("Client disconnected");
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0]["component"].get<std::string>(), "game_server");
    EXPECT_TRUE(events[0]["data"].contains("session_id"));

    server.stop();
}

// ===========================================================================
// Group F: Edge Cases (3 tests)
// ===========================================================================

TEST_F(GameServerTest, EdgeCase_DoubleStartIsHarmless)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();
    auto port_first = server.port();
    server.start(); // Second call should be a no-op
    EXPECT_TRUE(server.is_running());
    EXPECT_EQ(server.port(), port_first);
    server.stop();
}

TEST_F(GameServerTest, EdgeCase_StopBeforeStartIsHarmless)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.stop(); // Should not crash or throw
    EXPECT_FALSE(server.is_running());
}

TEST_F(GameServerTest, EdgeCase_RapidConnectDisconnect)
{
    wow::GameServerConfig cfg{0};
    wow::GameServer server(cfg);
    server.start();

    asio::io_context client_ctx;
    // Rapidly connect and disconnect 5 clients
    for (int i = 0; i < 5; ++i) {
        auto sock = connect_client(client_ctx, server.port());
        sock.close();
    }

    // Wait for all disconnects to process — count should return to 0
    EXPECT_TRUE(wait_for([&] { return server.connection_count() == 0; }, 2000ms));
    EXPECT_EQ(server.connection_count(), 0u);

    server.stop();
}
