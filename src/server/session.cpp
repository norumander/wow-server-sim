#include "server/session.h"

namespace wow {

std::atomic<uint64_t> Session::next_id_{1};

Session::Session()
    : session_id_(next_id_.fetch_add(1))
    , state_(SessionState::CONNECTING)
{
}

uint64_t Session::session_id() const
{
    return session_id_;
}

SessionState Session::state() const
{
    return state_;
}

bool Session::transition(SessionEvent /*event*/)
{
    // Stub — always rejects. Implemented in the GREEN step.
    return false;
}

std::string_view session_state_to_string(SessionState /*state*/)
{
    // Stub — implemented in the GREEN step.
    return "UNKNOWN";
}

std::string_view session_event_to_string(SessionEvent /*event*/)
{
    // Stub — implemented in the GREEN step.
    return "UNKNOWN";
}

}  // namespace wow
