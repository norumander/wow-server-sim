#include "control/control_channel.h"

#include "server/telemetry/logger.h"

namespace wow {

// ---------------------------------------------------------------------------
// CommandQueue
// ---------------------------------------------------------------------------

void CommandQueue::push(ControlCommand cmd)
{
    (void)cmd;
}

std::vector<ControlCommand> CommandQueue::drain()
{
    return {};
}

size_t CommandQueue::size() const
{
    return 0;
}

bool CommandQueue::empty() const
{
    return true;
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
}

void ControlChannel::start()
{
}

void ControlChannel::stop()
{
}

bool ControlChannel::is_running() const
{
    return false;
}

uint16_t ControlChannel::port() const
{
    return 0;
}

size_t ControlChannel::client_count() const
{
    return 0;
}

void ControlChannel::process_pending_commands()
{
}

void ControlChannel::do_accept()
{
}

void ControlChannel::handle_client(std::shared_ptr<asio::ip::tcp::socket> /*socket*/)
{
}

void ControlChannel::do_read_line(std::shared_ptr<asio::ip::tcp::socket> /*socket*/,
                                  std::shared_ptr<asio::streambuf> /*buffer*/)
{
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
