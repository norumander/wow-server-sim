include(GoogleTest)

# Unit test executable — sources added as TDD produces them
add_executable(unit_tests
    unit/test_logger.cpp
    unit/test_game_loop.cpp
    unit/test_session.cpp
    unit/test_game_server.cpp
    ${CMAKE_SOURCE_DIR}/src/server/telemetry/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/server/game_loop.cpp
    ${CMAKE_SOURCE_DIR}/src/server/session.cpp
    ${CMAKE_SOURCE_DIR}/src/server/game_server.cpp
    ${CMAKE_SOURCE_DIR}/src/server/connection.cpp
)
target_link_libraries(unit_tests
    PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
        asio
        $<$<NOT:$<BOOL:${WIN32}>>:pthread>
)
target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
gtest_discover_tests(unit_tests)
