cmake_minimum_required(VERSION 3.22)
project(hamdeck-cpp VERSION 0.1.0 LANGUAGES CXX)
add_compile_definitions(HAMDECK_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# civetweb, pinned. MIT. Chosen over cpp-httplib because it serves HTTP and
# WebSocket ON THE SAME PORT, which is not a preference - the client connects to
# ws://host:<dashboard port>/ws, so /ws cannot move to a port of its own without
# breaking every existing client. cpp-httplib has no WebSocket support whatsoever
# (it defines the 426 status constant and nothing else) and cannot hand off the
# socket, so it could never serve /ws.
#
# The migration was done while the route surface was 5 routes rather than 141.
include(FetchContent)
set(CIVETWEB_ENABLE_WEBSOCKETS ON  CACHE BOOL "" FORCE)
set(CIVETWEB_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_CXX        OFF CACHE BOOL "" FORCE)
set(CIVETWEB_INSTALL_EXECUTABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(civetweb
  GIT_REPOSITORY https://github.com/civetweb/civetweb.git
  GIT_TAG        v1.16
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(civetweb)

find_package(OpenSSL REQUIRED)

# nlohmann/json, pinned. MIT, header-only. Added when config landed: hand-rolled
# string scraping is fine for a two-field login body and is a liability for a
# config file, where a silently mis-parsed value is a setting that quietly is not
# what the operator wrote.
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(hamdeck-host src/main.cpp src/log.cpp src/amp_tuner.cpp src/api.cpp src/transmit_routes.cpp src/cw_text.cpp src/http.cpp src/audio.cpp src/cat_sim.cpp src/serial_cat.cpp src/radio.cpp src/auth.cpp src/config.cpp src/tx_audio.cpp src/cat_proxy.cpp src/alsa_devices.cpp src/rig_cal.cpp src/alsa_audio.cpp src/tgxl.cpp src/recorder.cpp
    src/session_stats.cpp)
target_link_libraries(hamdeck-host PRIVATE civetweb-c-library OpenSSL::Crypto nlohmann_json::nlohmann_json asound)
target_compile_options(hamdeck-host PRIVATE -Wall -Wextra)

# CI must RUN things, not just build them (CARRYOVER.md section 8): the .NET
# client shipped a release that could not launch while every test passed.
enable_testing()
add_executable(test_staleness tests/test_staleness.cpp src/cat_sim.cpp src/radio.cpp src/session_stats.cpp)
add_test(NAME staleness COMMAND test_staleness)

add_executable(test_auth tests/test_auth.cpp src/auth.cpp)
target_link_libraries(test_auth PRIVATE OpenSSL::Crypto)
add_test(NAME auth COMMAND test_auth)

add_executable(test_watchdog tests/test_watchdog.cpp src/cat_sim.cpp src/radio.cpp src/session_stats.cpp)
add_test(NAME watchdog COMMAND test_watchdog)

add_executable(test_audio_queue tests/test_audio_queue.cpp src/audio.cpp src/http.cpp src/recorder.cpp)
target_link_libraries(test_audio_queue PRIVATE civetweb-c-library)
add_test(NAME audio_queue COMMAND test_audio_queue)

add_executable(test_recorder tests/test_recorder.cpp src/recorder.cpp)
add_test(NAME recorder COMMAND test_recorder)

add_executable(test_session_stats tests/test_session_stats.cpp src/session_stats.cpp)
add_test(NAME session_stats COMMAND test_session_stats)

# Drives the REAL serial code through a pty with a fake rig on the far end, so
# every serial failure mode is found without the station off the air.
add_executable(test_serial tests/test_serial.cpp src/serial_cat.cpp)
target_link_libraries(test_serial PRIVATE util)
add_test(NAME serial COMMAND test_serial)

add_executable(test_config tests/test_config.cpp src/config.cpp)
target_link_libraries(test_config PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME config COMMAND test_config)

add_executable(test_tx_audio tests/test_tx_audio.cpp src/tx_audio.cpp)
add_test(NAME tx_audio COMMAND test_tx_audio)

add_executable(test_rig_cal tests/test_rig_cal.cpp src/rig_cal.cpp)
add_test(NAME rig_cal COMMAND test_rig_cal)

add_executable(test_queue_order tests/test_queue_order.cpp src/cat_sim.cpp src/radio.cpp src/session_stats.cpp)
add_test(NAME queue_order COMMAND test_queue_order)

add_executable(test_cw tests/test_cw.cpp src/cw_text.cpp)
add_test(NAME cw COMMAND test_cw)

add_executable(test_transmit_gate tests/test_transmit_gate.cpp src/transmit_routes.cpp)
add_test(NAME transmit_gate COMMAND test_transmit_gate)

add_executable(test_remote_active tests/test_remote_active.cpp src/auth.cpp)
target_link_libraries(test_remote_active PRIVATE OpenSSL::Crypto)
add_test(NAME remote_active COMMAND test_remote_active)

add_executable(test_admin tests/test_admin.cpp src/auth.cpp)
target_link_libraries(test_admin PRIVATE OpenSSL::Crypto)
add_test(NAME admin COMMAND test_admin)
