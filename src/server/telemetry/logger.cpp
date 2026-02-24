#include "server/telemetry/logger.h"

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

void Logger::log(LogType /*type*/,
                 std::string_view /*component*/,
                 std::string_view /*message*/,
                 const nlohmann::json& /*data*/)
{
    // Stub — intentionally does nothing so tests fail (RED step).
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

nlohmann::json Logger::format_entry(LogType /*type*/,
                                    std::string_view /*component*/,
                                    std::string_view /*message*/,
                                    const nlohmann::json& /*data*/)
{
    // Stub — returns empty object (RED step).
    return nlohmann::json::object();
}

std::string Logger::now_iso8601()
{
    // Stub — returns empty string (RED step).
    return "";
}

std::string_view Logger::log_type_to_string(LogType /*type*/)
{
    // Stub — returns empty string (RED step).
    return "";
}

void Logger::write(const std::string& /*line*/)
{
    // Stub — intentionally does nothing (RED step).
}

}  // namespace wow
