#include "control/control_channel.h"

#include <algorithm>
#include <string>

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// CommandQueue
// ---------------------------------------------------------------------------

void CommandQueue::push(ControlCommand cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(std::move(cmd));
}

std::vector<ControlCommand> CommandQueue::drain()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ControlCommand> result;
    result.swap(commands_);
    return result;
}

size_t CommandQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

bool CommandQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.empty();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string fault_mode_to_string(FaultMode mode)
{
    switch (mode) {
        case FaultMode::TICK_SCOPED: return "tick_scoped";
        case FaultMode::AMBIENT:     return "ambient";
        default:                     return "unknown";
    }
}

nlohmann::json fault_status_to_json(const FaultStatus& status)
{
    return nlohmann::json{
        {"id", status.id},
        {"mode", fault_mode_to_string(status.mode)},
        {"active", status.active},
        {"activations", status.activations},
        {"ticks_elapsed", status.ticks_elapsed},
        {"config", status.config}
    };
}

// ---------------------------------------------------------------------------
// ControlChannel
// ---------------------------------------------------------------------------

ControlChannel::ControlChannel(FaultRegistry& registry,
                               const ControlChannelConfig& config)
    : registry_(registry)
    , config_(config)
    , acceptor_(io_context_)
{
}

ControlChannel::~ControlChannel()
{
    if (running_.load()) {
        stop();
    }
}

void ControlChannel::start()
{
    // Guard against double-start.
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    // Bind acceptor to configured port (0 = OS-assigned).
    asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::loopback(), config_.port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    if (Logger::is_initialized()) {
        Logger::instance().event("control_channel", "Control channel started", {
            {"port", acceptor_.local_endpoint().port()},
        });
    }

    do_accept();

    // Spawn network thread running io_context.
    network_thread_ = std::thread([this] {
        io_context_.run();
    });
}

void ControlChannel::stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    // Stop accepting new connections.
    asio::error_code ec;
    acceptor_.close(ec);

    // Close all connected clients.
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& weak_sock : clients_) {
            if (auto sock = weak_sock.lock()) {
                asio::error_code close_ec;
                sock->close(close_ec);
            }
        }
        clients_.clear();
        client_count_.store(0);
    }

    // Stop io_context and join network thread.
    io_context_.stop();
    if (network_thread_.joinable()) {
        network_thread_.join();
    }

    if (Logger::is_initialized()) {
        Logger::instance().event("control_channel", "Control channel stopped", {});
    }
}

bool ControlChannel::is_running() const
{
    return running_.load();
}

uint16_t ControlChannel::port() const
{
    if (!running_.load()) {
        return 0;
    }
    asio::error_code ec;
    auto ep = acceptor_.local_endpoint(ec);
    if (ec) {
        return 0;
    }
    return ep.port();
}

size_t ControlChannel::client_count() const
{
    return client_count_.load();
}

void ControlChannel::process_pending_commands()
{
    auto commands = command_queue_.drain();
    for (auto& cmd : commands) {
        auto response = execute_command(cmd.request);
        if (cmd.on_complete) {
            cmd.on_complete(std::move(response));
        }
    }
}

void ControlChannel::do_accept()
{
    acceptor_.async_accept(
        [this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
            if (ec) {
                return;
            }

            auto sock_ptr = std::make_shared<asio::ip::tcp::socket>(std::move(socket));

            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.push_back(sock_ptr);
                client_count_.store(clients_.size());
            }

            if (Logger::is_initialized()) {
                Logger::instance().event("control_channel", "Control client connected", {
                    {"client_count", client_count_.load()},
                });
            }

            handle_client(sock_ptr);

            // Continue accepting.
            do_accept();
        });
}

void ControlChannel::handle_client(std::shared_ptr<asio::ip::tcp::socket> socket)
{
    auto buffer = std::make_shared<asio::streambuf>();
    do_read_line(socket, buffer);
}

void ControlChannel::do_read_line(std::shared_ptr<asio::ip::tcp::socket> socket,
                                  std::shared_ptr<asio::streambuf> buffer)
{
    asio::async_read_until(*socket, *buffer, '\n',
        [this, socket, buffer](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                // Client disconnected or read error — remove from client list.
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.erase(
                        std::remove_if(clients_.begin(), clients_.end(),
                            [&socket](const std::weak_ptr<asio::ip::tcp::socket>& wp) {
                                auto sp = wp.lock();
                                return !sp || sp == socket;
                            }),
                        clients_.end());
                    client_count_.store(clients_.size());
                }

                if (Logger::is_initialized()) {
                    Logger::instance().event("control_channel", "Control client disconnected", {
                        {"client_count", client_count_.load()},
                    });
                }
                return;
            }

            // Extract one line from the buffer.
            std::istream stream(buffer.get());
            std::string line;
            std::getline(stream, line);

            // Remove trailing \r if present (Windows clients).
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Parse JSON and push command to queue with response callback.
            try {
                auto request = nlohmann::json::parse(line);

                // Capture socket and io_context for response write-back.
                // on_complete posts the response write to the network thread.
                auto& ctx = io_context_;
                command_queue_.push({
                    std::move(request),
                    [socket, &ctx](nlohmann::json response) {
                        std::string response_line = response.dump() + "\n";
                        auto response_buf = std::make_shared<std::string>(std::move(response_line));
                        asio::post(ctx, [socket, response_buf]() {
                            asio::error_code write_ec;
                            asio::write(*socket, asio::buffer(*response_buf), write_ec);
                        });
                    }
                });
            } catch (const nlohmann::json::parse_error& e) {
                // JSON parse error — respond directly on network thread (no queue round-trip).
                nlohmann::json error_response = {
                    {"success", false},
                    {"error", std::string("Invalid JSON: ") + e.what()}
                };
                std::string error_line = error_response.dump() + "\n";
                auto error_buf = std::make_shared<std::string>(std::move(error_line));
                asio::error_code write_ec;
                asio::write(*socket, asio::buffer(*error_buf), write_ec);
            }

            // Continue reading.
            do_read_line(socket, buffer);
        });
}

nlohmann::json ControlChannel::execute_command(const nlohmann::json& request)
{
    // Validate required 'command' field.
    if (!request.contains("command") || !request["command"].is_string()) {
        return {
            {"success", false},
            {"error", "Missing required field: command"}
        };
    }

    std::string command = request["command"].get<std::string>();

    if (command == "activate") {
        return handle_activate(request);
    } else if (command == "deactivate") {
        return handle_deactivate(request);
    } else if (command == "deactivate_all") {
        return handle_deactivate_all(request);
    } else if (command == "status") {
        return handle_status(request);
    } else if (command == "list") {
        return handle_list(request);
    }

    return {
        {"success", false},
        {"error", "Unknown command: " + command}
    };
}

nlohmann::json ControlChannel::handle_activate(const nlohmann::json& request)
{
    if (!request.contains("fault_id") || !request["fault_id"].is_string()) {
        return {
            {"success", false},
            {"error", "Missing required field: fault_id"}
        };
    }

    std::string fault_id = request["fault_id"].get<std::string>();

    FaultConfig config;
    if (request.contains("params")) {
        config.params = request["params"];
    }
    if (request.contains("target_zone_id")) {
        config.target_zone_id = request["target_zone_id"].get<uint32_t>();
    }
    if (request.contains("duration_ticks")) {
        config.duration_ticks = request["duration_ticks"].get<uint64_t>();
    }

    bool success = registry_.activate(fault_id, config);
    if (!success) {
        return {
            {"success", false},
            {"error", "Failed to activate fault: " + fault_id}
        };
    }

    return {
        {"success", true},
        {"command", "activate"},
        {"fault_id", fault_id}
    };
}

nlohmann::json ControlChannel::handle_deactivate(const nlohmann::json& request)
{
    if (!request.contains("fault_id") || !request["fault_id"].is_string()) {
        return {
            {"success", false},
            {"error", "Missing required field: fault_id"}
        };
    }

    std::string fault_id = request["fault_id"].get<std::string>();

    bool success = registry_.deactivate(fault_id);
    if (!success) {
        return {
            {"success", false},
            {"error", "Failed to deactivate fault: " + fault_id}
        };
    }

    return {
        {"success", true},
        {"command", "deactivate"},
        {"fault_id", fault_id}
    };
}

nlohmann::json ControlChannel::handle_deactivate_all(const nlohmann::json& /*request*/)
{
    registry_.deactivate_all();
    return {
        {"success", true},
        {"command", "deactivate_all"}
    };
}

nlohmann::json ControlChannel::handle_status(const nlohmann::json& request)
{
    if (!request.contains("fault_id") || !request["fault_id"].is_string()) {
        return {
            {"success", false},
            {"error", "Missing required field: fault_id"}
        };
    }

    std::string fault_id = request["fault_id"].get<std::string>();

    auto status = registry_.fault_status(fault_id);
    if (!status.has_value()) {
        return {
            {"success", false},
            {"error", "Unknown fault: " + fault_id}
        };
    }

    return {
        {"success", true},
        {"command", "status"},
        {"fault_id", fault_id},
        {"status", fault_status_to_json(status.value())}
    };
}

nlohmann::json ControlChannel::handle_list(const nlohmann::json& /*request*/)
{
    auto all = registry_.all_status();
    nlohmann::json faults = nlohmann::json::array();
    for (const auto& s : all) {
        faults.push_back(fault_status_to_json(s));
    }

    return {
        {"success", true},
        {"command", "list"},
        {"faults", faults}
    };
}

}  // namespace wow
