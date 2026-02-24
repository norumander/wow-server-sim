#pragma once

#include <fstream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace wow {

/// Schema version for telemetry JSON entries. Increment when the entry
/// format changes in a backwards-incompatible way.
constexpr int kTelemetrySchemaVersion = 1;

/// Types of telemetry log entries.
enum class LogType {
    metric,
    event,
    health,
    error,
};

/// Configuration for the telemetry logger.
struct LoggerConfig {
    /// Path to the JSON Lines log file. Empty disables file output.
    std::string file_path;

    /// Whether to also write to stdout.
    bool stdout_enabled = false;

    /// Optional custom sink (e.g. ostringstream for tests). Not owned.
    std::ostream* custom_sink = nullptr;
};

/// Structured JSON telemetry logger (singleton).
///
/// Emits newline-delimited JSON entries to configurable sinks (file, stdout,
/// custom ostream). Thread-safe for concurrent log() calls.
///
/// Usage:
///   Logger::initialize({.file_path = "telemetry.jsonl", .stdout_enabled = true});
///   Logger::instance().event("session", "Player connected", {{"session_id", 42}});
///   Logger::reset();
class Logger {
public:
    /// Initialize the singleton with the given configuration.
    /// Must be called before instance(). Calling twice without reset() is an error.
    static void initialize(const LoggerConfig& config);

    /// Access the singleton instance. Throws if not initialized.
    static Logger& instance();

    /// Tear down the singleton, flushing and closing all sinks.
    static void reset();

    /// Returns true if the singleton has been initialized.
    static bool is_initialized();

    /// Emit a structured log entry.
    ///
    /// @param type     Log entry type (metric, event, health, error).
    /// @param component  Originating subsystem (e.g. "session", "combat").
    /// @param message    Human-readable description of the entry.
    /// @param data       Optional structured payload (appears under "data" key).
    void log(LogType type,
             std::string_view component,
             std::string_view message,
             const nlohmann::json& data = nlohmann::json::object());

    /// Convenience: emit a metric entry.
    void metric(std::string_view component,
                std::string_view message,
                const nlohmann::json& data = nlohmann::json::object());

    /// Convenience: emit an event entry.
    void event(std::string_view component,
               std::string_view message,
               const nlohmann::json& data = nlohmann::json::object());

    /// Convenience: emit a health entry.
    void health(std::string_view component,
                std::string_view message,
                const nlohmann::json& data = nlohmann::json::object());

    /// Convenience: emit an error entry.
    void error(std::string_view component,
               std::string_view message,
               const nlohmann::json& data = nlohmann::json::object());

    // Non-copyable, non-movable (singleton).
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

private:
    Logger() = default;
    ~Logger() = default;

    /// Format a log entry as a JSON object (lock-free, no side effects).
    static nlohmann::json format_entry(LogType type,
                                       std::string_view component,
                                       std::string_view message,
                                       const nlohmann::json& data);

    /// Return the current UTC time as ISO 8601 with milliseconds.
    static std::string now_iso8601();

    /// Convert LogType enum to its string representation.
    static std::string_view log_type_to_string(LogType type);

    /// Write a formatted JSON line to all configured sinks.
    void write(const std::string& line);

    LoggerConfig config_;
    std::ofstream file_stream_;
    std::mutex write_mutex_;

    static Logger* instance_;
    static std::mutex init_mutex_;
};

}  // namespace wow
