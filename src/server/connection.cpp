#include "server/connection.h"

#include <string>

#include "server/telemetry/logger.h"

namespace wow {

Connection::Connection(asio::ip::tcp::socket socket, DisconnectCallback on_disconnect)
    : socket_(std::move(socket))
    , on_disconnect_(std::move(on_disconnect))
    , read_buffer_{}
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

void Connection::do_read()
{
    // Capture shared_ptr to prevent destruction while async op is pending.
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(read_buffer_),
        [self](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                // EOF or error — client disconnected.
                // Transition session: CONNECTING + DISCONNECT → DESTROYED.
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
            // Data received but discarded — read loop only detects disconnect.
            self->do_read();
        });
}

}  // namespace wow
