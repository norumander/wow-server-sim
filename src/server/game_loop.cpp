#include "server/game_loop.h"

#include <chrono>

#include "server/telemetry/logger.h"

namespace wow {

GameLoop::GameLoop(const GameLoopConfig& config)
    : config_(config)
    , tick_interval_(std::chrono::nanoseconds(
          static_cast<int64_t>(1e9 / config.tick_rate_hz)))
{
}

GameLoop::~GameLoop()
{
    stop();
}

void GameLoop::on_tick(TickCallback callback)
{
    callbacks_.push_back(std::move(callback));
}

void GameLoop::start()
{
    // Stub — does not launch a thread yet.
}

void GameLoop::run()
{
    // Stub — does not execute the loop yet.
}

void GameLoop::stop()
{
    // Stub — no-op.
}

bool GameLoop::is_running() const
{
    return running_.load();
}

uint64_t GameLoop::tick_count() const
{
    return tick_count_.load();
}

std::chrono::nanoseconds GameLoop::tick_interval() const
{
    return tick_interval_;
}

void GameLoop::loop_body()
{
    // Stub — not implemented yet.
}

void GameLoop::execute_tick()
{
    // Stub — not implemented yet.
}

}  // namespace wow
