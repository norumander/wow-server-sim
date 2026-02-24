#include "server/game_server.h"

#include <string>

#include "server/telemetry/logger.h"

namespace wow {

GameServer::GameServer(const GameServerConfig& config)
    : config_(config)
    , acceptor_(io_context_)
{
}

GameServer::~GameServer()
{
    if (running_.load()) {
        stop();
    }
}

void GameServer::start()
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
        Logger::instance().event("game_server", "Server started", {
            {"port", acceptor_.local_endpoint().port()},
        });
    }

    do_accept();

    // Spawn network thread running io_context.
    network_thread_ = std::thread([this] {
        io_context_.run();
    });
}

void GameServer::stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    // Stop accepting new connections.
    asio::error_code ec;
    acceptor_.close(ec);

    // Close all active connections and transition their sessions.
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& [id, conn] : connections_) {
            conn->close();
        }
        connections_.clear();
        connection_count_.store(0);
    }

    // Stop io_context and join network thread.
    io_context_.stop();
    if (network_thread_.joinable()) {
        network_thread_.join();
    }

    if (Logger::is_initialized()) {
        Logger::instance().event("game_server", "Server stopped", {});
    }
}

bool GameServer::is_running() const
{
    return running_.load();
}

uint16_t GameServer::port() const
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

size_t GameServer::connection_count() const
{
    return connection_count_.load();
}

void GameServer::do_accept()
{
    acceptor_.async_accept(
        [this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
            if (ec) {
                // Acceptor was closed (during stop) or other error — stop loop.
                return;
            }

            auto conn = std::make_shared<Connection>(
                std::move(socket),
                [this](uint64_t session_id) { on_disconnect(session_id); });

            auto sid = conn->session_id();
            auto remote = conn->remote_endpoint_string();

            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                connections_[sid] = conn;
                connection_count_.store(connections_.size());
            }

            if (Logger::is_initialized()) {
                Logger::instance().event("game_server", "Connection accepted", {
                    {"session_id", sid},
                    {"remote_endpoint", remote},
                });
            }

            // Start async read loop (must be after storing in map so
            // shared_from_this() has a live shared_ptr).
            conn->start();

            // Continue accepting.
            do_accept();
        });
}

void GameServer::on_disconnect(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(session_id);
    connection_count_.store(connections_.size());
}

}  // namespace wow
