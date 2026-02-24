#include "server/telemetry/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace wow {

Logger* Logger::instance_ = nullptr;
std::mutex Logger::init_mutex_;

void Logger::initialize(const LoggerConfig& config)
{
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (instance_) {
        throw std::runtime_error("Logger already initialized. Call reset() first.");
    }
    instance_ = new Logger();
    instance_->config_ = config;

    if (!config.file_path.empty()) {
        instance_->file_stream_.open(config.file_path, std::ios::app);
        if (!instance_->file_stream_.is_open()) {
            delete instance_;
            instance_ = nullptr;
            throw std::runtime_error("Failed to open log file: " + config.file_path);
        }
    }
}

Logger& Logger::instance()
{
    if (!instance_) {
        throw std::runtime_error("Logger not initialized. Call initialize() first.");
    }
    return *instance_;
}

void Logger::reset()
{
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (instance_) {
        if (instance_->file_stream_.is_open()) {
            instance_->file_stream_.close();
        }
        delete instance_;
        instance_ = nullptr;
    }
}

bool Logger::is_initialized()
{
    return instance_ != nullptr;
}

void Logger::log(LogType type,
                 std::string_view component,
                 std::string_view message,
                 const nlohmann::json& data)
{
    nlohmann::json entry = format_entry(type, component, message, data);
    std::string line = entry.dump() + "\n";
    write(line);
}

void Logger::metric(std::string_view component,
                    std::string_view message,
                    const nlohmann::json& data)
{
    log(LogType::metric, component, message, data);
}

void Logger::event(std::string_view component,
                   std::string_view message,
                   const nlohmann::json& data)
{
    log(LogType::event, component, message, data);
}

void Logger::health(std::string_view component,
                    std::string_view message,
                    const nlohmann::json& data)
{
    log(LogType::health, component, message, data);
}

void Logger::error(std::string_view component,
                   std::string_view message,
                   const nlohmann::json& data)
{
    log(LogType::error, component, message, data);
}

nlohmann::json Logger::format_entry(LogType type,
                                    std::string_view component,
                                    std::string_view message,
                                    const nlohmann::json& data)
{
    nlohmann::json entry;
    entry["v"] = 1;
    entry["timestamp"] = now_iso8601();
    entry["type"] = log_type_to_string(type);
    entry["component"] = std::string(component);
    entry["message"] = std::string(message);
    if (!data.empty()) {
        entry["data"] = data;
    }
    return entry;
}

std::string Logger::now_iso8601()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::tm utc_tm{};
#ifdef _WIN32
    gmtime_s(&utc_tm, &time_t_now);
#else
    gmtime_r(&time_t_now, &utc_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    oss << 'Z';
    return oss.str();
}

std::string_view Logger::log_type_to_string(LogType type)
{
    switch (type) {
    case LogType::metric: return "metric";
    case LogType::event:  return "event";
    case LogType::health: return "health";
    case LogType::error:  return "error";
    }
    return "unknown";
}

void Logger::write(const std::string& line)
{
    std::lock_guard<std::mutex> lock(write_mutex_);

    if (config_.stdout_enabled) {
        std::cout << line;
        std::cout.flush();
    }

    if (file_stream_.is_open()) {
        file_stream_ << line;
        file_stream_.flush();
    }

    if (config_.custom_sink) {
        *config_.custom_sink << line;
        config_.custom_sink->flush();
    }
}

}  // namespace wow
