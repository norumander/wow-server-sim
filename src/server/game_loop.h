#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace wow {

/// Configuration for the game loop timing.
struct GameLoopConfig {
    /// Tick rate in Hz. Default 20 Hz matches WoW's actual server tick rate
    /// (50ms per tick).
    double tick_rate_hz = 20.0;
};

/// Fixed-rate game loop that drives all server processing.
///
/// Executes registered callbacks at a fixed tick rate using a
/// sleep-for-remainder timing strategy. On overrun (callbacks take longer
/// than the tick interval), the loop skips the sleep and continues
/// immediately — no debt accumulation.
///
/// Thread model: the loop either blocks the caller (run()) or runs on a
/// background thread (start()/stop()). Callbacks are registered before
/// starting; the callback list is not thread-safe by design.
class GameLoop {
public:
    /// Signature for tick callbacks. Receives the current tick number
    /// (0-indexed, sequential).
    using TickCallback = std::function<void(uint64_t)>;

    /// Construct a game loop with the given configuration.
    explicit GameLoop(const GameLoopConfig& config = {});

    /// Destructor calls stop() — RAII for thread ownership.
    ~GameLoop();

    // Non-copyable, non-movable (owns a thread).
    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;
    GameLoop(GameLoop&&) = delete;
    GameLoop& operator=(GameLoop&&) = delete;

    /// Register a callback to be invoked on every tick. Must be called
    /// before start() or run(). Not thread-safe.
    void on_tick(TickCallback callback);

    /// Start the loop on a background thread. Returns immediately.
    void start();

    /// Run the loop on the calling thread. Blocks until stop() is called
    /// from another thread.
    void run();

    /// Signal the loop to stop and join the background thread (if any).
    /// Thread-safe. Idempotent — safe to call multiple times.
    void stop();

    /// Returns true if the loop is currently running.
    bool is_running() const;

    /// Returns the number of ticks executed so far.
    uint64_t tick_count() const;

    /// Returns the configured tick interval.
    std::chrono::nanoseconds tick_interval() const;

private:
    /// The core loop: execute ticks at fixed rate until running_ is false.
    void loop_body();

    /// Execute all registered callbacks for the current tick and emit
    /// telemetry.
    void execute_tick();

    GameLoopConfig config_;
    std::chrono::nanoseconds tick_interval_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> tick_count_{0};
    std::vector<TickCallback> callbacks_;
    std::thread thread_;
};

}  // namespace wow
