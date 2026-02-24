#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "server/session.h"

namespace wow {

/// Bridges the network layer (TCP socket) and the game layer (Session).
///
/// Each Connection owns a Session by value. Uses enable_shared_from_this
/// for safe async callback capture — the async read loop must prevent
/// the Connection from being destroyed while an I/O operation is pending.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    /// Callback invoked when a connection detects client disconnect.
    using DisconnectCallback = std::function<void(uint64_t)>;

    /// Construct a Connection wrapping an accepted TCP socket.
    ///
    /// @param socket         Accepted TCP socket (moved in).
    /// @param on_disconnect  Callback fired on EOF/error with session_id.
    Connection(asio::ip::tcp::socket socket, DisconnectCallback on_disconnect);

    /// Begin the async read loop for disconnect detection.
    ///
    /// Must be called after the Connection is stored in a shared_ptr
    /// (required for shared_from_this() to work).
    void start();

    /// Close the socket gracefully.
    void close();

    /// Return the session's unique identifier.
    uint64_t session_id() const;

    /// Return the session's current state.
    SessionState session_state() const;

    /// Return a string describing the remote endpoint (e.g. "127.0.0.1:54321").
    std::string remote_endpoint_string() const;

private:
    /// Async read loop — detects disconnect via EOF or error.
    void do_read();

    asio::ip::tcp::socket socket_;
    Session session_;
    DisconnectCallback on_disconnect_;
    std::array<char, 1024> read_buffer_;
};

}  // namespace wow
