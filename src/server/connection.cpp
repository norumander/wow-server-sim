#include "server/connection.h"

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
    // Stub — async read loop not yet implemented.
}

void Connection::close()
{
    // Stub — graceful socket shutdown not yet implemented.
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
    return "stub";
}

void Connection::do_read()
{
    // Stub — async read not yet implemented.
}

}  // namespace wow
