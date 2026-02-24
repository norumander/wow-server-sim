#include "server/game_server.h"

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
    // Stub — not yet implemented.
}

void GameServer::stop()
{
    // Stub — not yet implemented.
}

bool GameServer::is_running() const
{
    return running_.load();
}

uint16_t GameServer::port() const
{
    return 0; // Stub
}

size_t GameServer::connection_count() const
{
    return connection_count_.load();
}

void GameServer::do_accept()
{
    // Stub — async accept loop not yet implemented.
}

void GameServer::on_disconnect(uint64_t /*session_id*/)
{
    // Stub — connection removal not yet implemented.
}

}  // namespace wow
