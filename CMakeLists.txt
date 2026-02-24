cmake_minimum_required(VERSION 3.14)
project(wow-server-sim
    VERSION 0.1.0
    DESCRIPTION "WoW Server Simulator — reliability engineering demo"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warnings — compiler-appropriate flags
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# Standalone Asio (non-Boost, header-only)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
    target_link_libraries(asio INTERFACE ws2_32 mswsock)
endif()

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# GoogleTest — force shared CRT on Windows to avoid /MT vs /MD mismatch
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Server executable
# ---------------------------------------------------------------------------
add_executable(wow-server-sim
    src/server/main.cpp
    src/server/game_loop.cpp
    src/server/game_server.cpp
    src/server/connection.cpp
    src/server/session.cpp
    src/server/telemetry/logger.cpp
    src/server/events/event.cpp
    src/server/events/movement.cpp
    src/server/events/spellcast.cpp
    src/server/events/combat.cpp
    src/server/events/event_queue.cpp
    src/server/world/entity.cpp
    src/server/world/zone.cpp
    src/server/world/zone_manager.cpp
)

target_link_libraries(wow-server-sim
    PRIVATE
        asio
        nlohmann_json::nlohmann_json
        $<$<NOT:$<BOOL:${WIN32}>>:pthread>
)

target_include_directories(wow-server-sim
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests/cpp)
