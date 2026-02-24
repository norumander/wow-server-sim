#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "server/fault/injector.h"

namespace wow {

/// Configuration for the control channel TCP server.
struct ControlChannelConfig {
    uint16_t port = 8081;  ///< TCP port. 0 = OS-assigned (used in tests).
};

/// A command received from a control channel client.
///
/// Parsed on the network thread; executed on the game thread via CommandQueue.
/// The on_complete callback sends the JSON response back to the client.
struct ControlCommand {
    nlohmann::json request;                           ///< Parsed JSON request
    std::function<void(nlohmann::json)> on_complete;  ///< Response callback
};

/// Thread-safe command queue for control channel → game thread communication.
///
/// Mirrors EventQueue: mutex-protected push from network thread,
/// swap-based drain from game thread. Only shared state between threads.
class CommandQueue {
public:
    /// Push a command (thread-safe, called from network thread).
    void push(ControlCommand cmd);

    /// Drain all queued commands, returning them and clearing the queue.
    /// Thread-safe, called from game thread in process_pending_commands().
    std::vector<ControlCommand> drain();

    /// Current queue depth (thread-safe).
    size_t size() const;

    /// Whether the queue is empty (thread-safe).
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::vector<ControlCommand> commands_;
};

/// Convert a FaultMode enum to its string representation.
std::string fault_mode_to_string(FaultMode mode);

/// Convert a FaultStatus snapshot to a JSON object for protocol responses.
nlohmann::json fault_status_to_json(const FaultStatus& status);

/// TCP control channel for runtime fault injection commands.
///
/// Runs on a separate TCP port from game traffic. Accepts newline-delimited
/// JSON commands (activate, deactivate, deactivate_all, status, list) and
/// routes them through a CommandQueue to the game thread for execution.
///
/// Thread safety: network thread pushes commands to CommandQueue; game thread
/// calls process_pending_commands() each tick to drain and execute them.
/// FaultRegistry is only ever touched by the game thread.
class ControlChannel {
public:
    /// Construct a control channel bound to the given fault registry.
    explicit ControlChannel(FaultRegistry& registry,
                            const ControlChannelConfig& config = {});

    /// Destructor stops the channel if still running (RAII).
    ~ControlChannel();

    /// Bind, listen, and spawn the network thread.
    void start();

    /// Stop the io_context, join the network thread, close all clients.
    void stop();

    /// Return whether the channel is currently running.
    bool is_running() const;

    /// Return the actual bound port (useful when config port=0).
    uint16_t port() const;

    /// Return the current number of connected control clients.
    size_t client_count() const;

    /// Drain and execute all pending commands on the game thread.
    /// Called once per game tick before registry.on_tick().
    void process_pending_commands();

    // Non-copyable, non-movable.
    ControlChannel(const ControlChannel&) = delete;
    ControlChannel& operator=(const ControlChannel&) = delete;

private:
    /// Start the async accept loop.
    void do_accept();

    /// Handle a connected client — start async line reading.
    void handle_client(std::shared_ptr<asio::ip::tcp::socket> socket);

    /// Async read loop: read one newline-delimited JSON line.
    void do_read_line(std::shared_ptr<asio::ip::tcp::socket> socket,
                      std::shared_ptr<asio::streambuf> buffer);

    /// Execute a parsed command against the fault registry (game thread).
    nlohmann::json execute_command(const nlohmann::json& request);

    /// Command handlers.
    nlohmann::json handle_activate(const nlohmann::json& request);
    nlohmann::json handle_deactivate(const nlohmann::json& request);
    nlohmann::json handle_deactivate_all(const nlohmann::json& request);
    nlohmann::json handle_status(const nlohmann::json& request);
    nlohmann::json handle_list(const nlohmann::json& request);

    FaultRegistry& registry_;
    ControlChannelConfig config_;
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread network_thread_;
    std::atomic<bool> running_{false};

    std::mutex clients_mutex_;
    std::vector<std::weak_ptr<asio::ip::tcp::socket>> clients_;
    std::atomic<size_t> client_count_{0};

    CommandQueue command_queue_;
};

}  // namespace wow
