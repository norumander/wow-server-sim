#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "server/session.h"
#include "server/telemetry/logger.h"

using json = nlohmann::json;

// ===========================================================================
// Test fixture — initializes Logger with ostringstream sink for telemetry
// tests. Construction-only tests (Group A) don't need the logger.
// ===========================================================================

class SessionTest : public ::testing::Test {
protected:
    std::ostringstream sink_;

    void SetUp() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    void TearDown() override
    {
        if (wow::Logger::is_initialized()) {
            wow::Logger::reset();
        }
    }

    /// Initialize the telemetry logger with our test sink.
    void init_logger()
    {
        wow::LoggerConfig config;
        config.custom_sink = &sink_;
        wow::Logger::initialize(config);
    }

    /// Parse all JSON lines from the sink into a vector.
    std::vector<json> parse_all_lines()
    {
        std::vector<json> entries;
        std::istringstream stream(sink_.str());
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                entries.push_back(json::parse(line));
            }
        }
        return entries;
    }

    /// Filter log entries by type and component.
    std::vector<json> filter_entries(const std::string& type,
                                     const std::string& component)
    {
        std::vector<json> result;
        for (const auto& entry : parse_all_lines()) {
            if (entry.value("type", "") == type &&
                entry.value("component", "") == component) {
                result.push_back(entry);
            }
        }
        return result;
    }

    /// Advance a session from CONNECTING to IN_WORLD.
    void advance_to_in_world(wow::Session& s)
    {
        ASSERT_TRUE(s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS));
        ASSERT_TRUE(s.transition(wow::SessionEvent::ENTER_WORLD));
        ASSERT_EQ(s.state(), wow::SessionState::IN_WORLD);
    }

    /// Advance a session from CONNECTING to DISCONNECTING (via IN_WORLD).
    void advance_to_disconnecting(wow::Session& s)
    {
        advance_to_in_world(s);
        ASSERT_TRUE(s.transition(wow::SessionEvent::DISCONNECT));
        ASSERT_EQ(s.state(), wow::SessionState::DISCONNECTING);
    }

    /// Advance a session from CONNECTING to TRANSFERRING (via IN_WORLD).
    void advance_to_transferring(wow::Session& s)
    {
        advance_to_in_world(s);
        ASSERT_TRUE(s.transition(wow::SessionEvent::BEGIN_TRANSFER));
        ASSERT_EQ(s.state(), wow::SessionState::TRANSFERRING);
    }
};

// ===========================================================================
// Group A: Construction (no Logger needed)
// ===========================================================================

TEST_F(SessionTest, Session_InitialStateIsConnecting)
{
    wow::Session s;
    EXPECT_EQ(s.state(), wow::SessionState::CONNECTING);
}

TEST_F(SessionTest, Session_SessionIdIsAssignedAtConstruction)
{
    wow::Session s;
    EXPECT_GT(s.session_id(), 0u);
}

TEST_F(SessionTest, Session_ConsecutiveSessionsGetUniqueIds)
{
    wow::Session s1;
    wow::Session s2;
    EXPECT_GT(s2.session_id(), s1.session_id());
}

// ===========================================================================
// Group B: Valid Transitions (one per table entry)
// ===========================================================================

TEST_F(SessionTest, Transition_ConnectingToAuthenticating)
{
    init_logger();
    wow::Session s;
    EXPECT_TRUE(s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS));
    EXPECT_EQ(s.state(), wow::SessionState::AUTHENTICATING);
}

TEST_F(SessionTest, Transition_AuthenticatingToInWorld)
{
    init_logger();
    wow::Session s;
    s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS);
    EXPECT_TRUE(s.transition(wow::SessionEvent::ENTER_WORLD));
    EXPECT_EQ(s.state(), wow::SessionState::IN_WORLD);
}

TEST_F(SessionTest, Transition_InWorldToDisconnecting)
{
    init_logger();
    wow::Session s;
    advance_to_in_world(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::DISCONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::DISCONNECTING);
}

TEST_F(SessionTest, Transition_InWorldToTransferring)
{
    init_logger();
    wow::Session s;
    advance_to_in_world(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::BEGIN_TRANSFER));
    EXPECT_EQ(s.state(), wow::SessionState::TRANSFERRING);
}

TEST_F(SessionTest, Transition_TransferringToInWorld)
{
    init_logger();
    wow::Session s;
    advance_to_transferring(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::TRANSFER_COMPLETE));
    EXPECT_EQ(s.state(), wow::SessionState::IN_WORLD);
}

TEST_F(SessionTest, Transition_TransferringToDisconnecting)
{
    init_logger();
    wow::Session s;
    advance_to_transferring(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::DISCONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::DISCONNECTING);
}

TEST_F(SessionTest, Transition_DisconnectingToAuthenticating)
{
    init_logger();
    wow::Session s;
    advance_to_disconnecting(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::RECONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::AUTHENTICATING);
}

TEST_F(SessionTest, Transition_DisconnectingToDestroyed)
{
    init_logger();
    wow::Session s;
    advance_to_disconnecting(s);
    EXPECT_TRUE(s.transition(wow::SessionEvent::TIMEOUT));
    EXPECT_EQ(s.state(), wow::SessionState::DESTROYED);
}

TEST_F(SessionTest, Transition_ConnectingToDestroyedOnEarlyDisconnect)
{
    init_logger();
    wow::Session s;
    EXPECT_TRUE(s.transition(wow::SessionEvent::DISCONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::DESTROYED);
}

TEST_F(SessionTest, Transition_AuthenticatingToDisconnecting)
{
    init_logger();
    wow::Session s;
    s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS);
    EXPECT_TRUE(s.transition(wow::SessionEvent::DISCONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::DISCONNECTING);
}

// ===========================================================================
// Group C: Invalid Transitions
// ===========================================================================

TEST_F(SessionTest, InvalidTransition_ConnectingRejectsEnterWorld)
{
    init_logger();
    wow::Session s;
    EXPECT_FALSE(s.transition(wow::SessionEvent::ENTER_WORLD));
    EXPECT_EQ(s.state(), wow::SessionState::CONNECTING);
}

TEST_F(SessionTest, InvalidTransition_DestroyedRejectsAllEvents)
{
    init_logger();
    wow::Session s;
    // Drive to DESTROYED via early disconnect
    s.transition(wow::SessionEvent::DISCONNECT);
    ASSERT_EQ(s.state(), wow::SessionState::DESTROYED);

    EXPECT_FALSE(s.transition(wow::SessionEvent::RECONNECT));
    EXPECT_EQ(s.state(), wow::SessionState::DESTROYED);
}

TEST_F(SessionTest, InvalidTransition_InWorldRejectsAuthenticateSuccess)
{
    init_logger();
    wow::Session s;
    advance_to_in_world(s);

    EXPECT_FALSE(s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS));
    EXPECT_EQ(s.state(), wow::SessionState::IN_WORLD);
}

// ===========================================================================
// Group D: Telemetry
// ===========================================================================

TEST_F(SessionTest, Telemetry_ValidTransitionEmitsEventLog)
{
    init_logger();
    wow::Session s;
    s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS);

    auto events = filter_entries("event", "session");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NE(events[0]["message"].get<std::string>().find("State transition"),
              std::string::npos);
}

TEST_F(SessionTest, Telemetry_ValidTransitionContainsCorrectFields)
{
    init_logger();
    wow::Session s;
    s.transition(wow::SessionEvent::AUTHENTICATE_SUCCESS);

    auto events = filter_entries("event", "session");
    ASSERT_EQ(events.size(), 1u);

    const auto& data = events[0]["data"];
    EXPECT_TRUE(data.contains("session_id"));
    EXPECT_TRUE(data.contains("from_state"));
    EXPECT_TRUE(data.contains("to_state"));
    EXPECT_TRUE(data.contains("event"));

    EXPECT_EQ(data["session_id"].get<uint64_t>(), s.session_id());
    EXPECT_EQ(data["from_state"].get<std::string>(), "CONNECTING");
    EXPECT_EQ(data["to_state"].get<std::string>(), "AUTHENTICATING");
    EXPECT_EQ(data["event"].get<std::string>(), "AUTHENTICATE_SUCCESS");
}

TEST_F(SessionTest, Telemetry_InvalidTransitionEmitsErrorLog)
{
    init_logger();
    wow::Session s;
    s.transition(wow::SessionEvent::ENTER_WORLD); // invalid from CONNECTING

    auto errors = filter_entries("error", "session");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0]["message"].get<std::string>().find("Invalid"),
              std::string::npos);
}

// ===========================================================================
// Group E: String Conversion
// ===========================================================================

TEST_F(SessionTest, StringConversion_AllStatesToString)
{
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::CONNECTING),
              "CONNECTING");
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::AUTHENTICATING),
              "AUTHENTICATING");
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::IN_WORLD),
              "IN_WORLD");
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::TRANSFERRING),
              "TRANSFERRING");
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::DISCONNECTING),
              "DISCONNECTING");
    EXPECT_EQ(wow::session_state_to_string(wow::SessionState::DESTROYED),
              "DESTROYED");
}

TEST_F(SessionTest, StringConversion_AllEventsToString)
{
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::AUTHENTICATE_SUCCESS),
              "AUTHENTICATE_SUCCESS");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::ENTER_WORLD),
              "ENTER_WORLD");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::DISCONNECT),
              "DISCONNECT");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::BEGIN_TRANSFER),
              "BEGIN_TRANSFER");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::TRANSFER_COMPLETE),
              "TRANSFER_COMPLETE");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::RECONNECT),
              "RECONNECT");
    EXPECT_EQ(wow::session_event_to_string(wow::SessionEvent::TIMEOUT),
              "TIMEOUT");
}
