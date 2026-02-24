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
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // Already running.
    }

    thread_ = std::thread([this]() { loop_body(); });
}

void GameLoop::run()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // Already running.
    }

    loop_body();
}

void GameLoop::stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // Not running or already stopping.
    }

    if (thread_.joinable()) {
        thread_.join();
    }
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
    Logger::instance().event("game_loop", "Game loop started", {
        {"tick_rate_hz", config_.tick_rate_hz},
        {"tick_interval_ms", std::chrono::duration_cast<
            std::chrono::microseconds>(tick_interval_).count() / 1000.0}
    });

    while (running_.load()) {
        auto tick_start = std::chrono::steady_clock::now();

        execute_tick();

        auto tick_end = std::chrono::steady_clock::now();
        auto elapsed = tick_end - tick_start;
        auto duration_ms = std::chrono::duration_cast<
            std::chrono::microseconds>(elapsed).count() / 1000.0;
        bool overrun = elapsed > tick_interval_;

        Logger::instance().metric("game_loop", "Tick completed", {
            {"tick", tick_count_.load() - 1},
            {"duration_ms", duration_ms},
            {"overrun", overrun}
        });

        if (!overrun) {
            std::this_thread::sleep_for(tick_interval_ - elapsed);
        }
    }

    Logger::instance().event("game_loop", "Game loop stopped", {
        {"total_ticks", tick_count_.load()}
    });
}

void GameLoop::execute_tick()
{
    uint64_t current_tick = tick_count_.fetch_add(1);
    for (auto& cb : callbacks_) {
        cb(current_tick);
    }
}

}  // namespace wow
