#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <asio.hpp>

#include "server/connection.h"
#include "server/session_event_queue.h"

namespace wow {

/// Configuration for the TCP game server.
struct GameServerConfig {
    uint16_t port = 8080;  ///< TCP port. 0 = OS-assigned (used in tests).
};

/// TCP game server that accepts client connections and creates Sessions.
///
/// Runs Asio's io_context on a dedicated network thread. Each accepted
/// connection is wrapped in a Connection object that owns a Session and
/// runs an async read loop for disconnect detection.
class GameServer {
public:
    /// Construct a game server with the given configuration.
    explicit GameServer(const GameServerConfig& config = {});

    /// Destructor stops the server if still running (RAII).
    ~GameServer();

    /// Bind, listen, and spawn the network thread.
    void start();

    /// Stop the io_context, join the network thread, close all connections.
    void stop();

    /// Return whether the server is currently running.
    bool is_running() const;

    /// Return the actual bound port (useful when config port=0).
    uint16_t port() const;

    /// Return the current number of active connections.
    size_t connection_count() const;

    /// Set the session event queue for connect/disconnect notifications.
    /// The queue is not owned — caller must ensure it outlives the server.
    void set_session_event_queue(SessionEventQueue* queue);

    // Non-copyable, non-movable.
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

private:
    /// Start the async accept loop.
    void do_accept();

    /// Remove a connection from the registry on disconnect.
    void on_disconnect(uint64_t session_id);

    GameServerConfig config_;
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread network_thread_;
    std::atomic<bool> running_{false};

    std::mutex connections_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> connections_;
    std::atomic<size_t> connection_count_{0};
    SessionEventQueue* session_event_queue_ = nullptr;
};

}  // namespace wow
