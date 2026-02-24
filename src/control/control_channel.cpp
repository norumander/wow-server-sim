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

std::string fault_mode_to_string(FaultMode /*mode*/)
{
    return "";
}

nlohmann::json fault_status_to_json(const FaultStatus& /*status*/)
{
    return {};
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

            // For now, just discard the line — command dispatch comes in commit 6.
            // Continue reading.
            do_read_line(socket, buffer);
        });
}

nlohmann::json ControlChannel::execute_command(const nlohmann::json& /*request*/)
{
    return {};
}

nlohmann::json ControlChannel::handle_activate(const nlohmann::json& /*request*/)
{
    return {};
}

nlohmann::json ControlChannel::handle_deactivate(const nlohmann::json& /*request*/)
{
    return {};
}

nlohmann::json ControlChannel::handle_deactivate_all(const nlohmann::json& /*request*/)
{
    return {};
}

nlohmann::json ControlChannel::handle_status(const nlohmann::json& /*request*/)
{
    return {};
}

nlohmann::json ControlChannel::handle_list(const nlohmann::json& /*request*/)
{
    return {};
}

}  // namespace wow
