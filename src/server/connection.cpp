#include "server/connection.h"

#include <istream>
#include <string>

#include <nlohmann/json.hpp>

#include "server/event_parser.h"
#include "server/telemetry/logger.h"

namespace wow {

Connection::Connection(asio::ip::tcp::socket socket, DisconnectCallback on_disconnect)
    : socket_(std::move(socket))
    , on_disconnect_(std::move(on_disconnect))
{
}

void Connection::start()
{
    do_read();
}

void Connection::close()
{
    asio::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

uint64_t Connection::session_id() const
{
    return session_.session_id();
}

SessionState Connection::session_state() const
{
    return session_.state();
}

std::string Connection::remote_endpoint_string() const
{
    asio::error_code ec;
    auto ep = socket_.remote_endpoint(ec);
    if (ec) {
        return "<closed>";
    }
    return ep.address().to_string() + ":" + std::to_string(ep.port());
}

void Connection::set_event_queue(EventQueue* queue)
{
    event_queue_ = queue;
}

void Connection::do_read()
{
    // Capture shared_ptr to prevent destruction while async op is pending.
    auto self = shared_from_this();
    asio::async_read_until(socket_, read_buffer_, '\n',
        [self](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                // EOF or error — client disconnected.
                self->session_.transition(SessionEvent::DISCONNECT);

                if (Logger::is_initialized()) {
                    Logger::instance().event("game_server", "Client disconnected", {
                        {"session_id", self->session_.session_id()},
                    });
                }

                if (self->on_disconnect_) {
                    self->on_disconnect_(self->session_.session_id());
                }
                return;
            }

            // Extract one line from the buffer.
            std::istream stream(&self->read_buffer_);
            std::string line;
            std::getline(stream, line);

            // Remove trailing \r if present (Windows clients).
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Parse JSON and push event to queue.
            if (!line.empty()) {
                try {
                    auto json_obj = nlohmann::json::parse(line);
                    auto event = EventParser::parse(json_obj);
                    if (event && self->event_queue_) {
                        self->event_queue_->push(std::move(event));
                    }
                } catch (const nlohmann::json::parse_error&) {
                    if (Logger::is_initialized()) {
                        Logger::instance().event("game_server", "Malformed JSON from client", {
                            {"session_id", self->session_.session_id()},
                        });
                    }
                }
            }

            // Continue reading.
            self->do_read();
        });
}

}  // namespace wow
