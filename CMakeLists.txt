cmake_minimum_required(VERSION 3.22)
project(hamdeck-cpp VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(hamdeck-host src/main.cpp src/api.cpp src/http.cpp src/audio.cpp src/cat_sim.cpp src/serial_cat.cpp src/radio.cpp src/auth.cpp)
target_link_libraries(hamdeck-host PRIVATE civetweb-c-library OpenSSL::Crypto)
target_compile_options(hamdeck-host PRIVATE -Wall -Wextra)

# CI must RUN things, not just build them (CARRYOVER.md section 8): the .NET
# client shipped a release that could not launch while every test passed.
enable_testing()
add_executable(test_staleness tests/test_staleness.cpp src/cat_sim.cpp src/radio.cpp)
add_test(NAME staleness COMMAND test_staleness)

add_executable(test_auth tests/test_auth.cpp src/auth.cpp)
target_link_libraries(test_auth PRIVATE OpenSSL::Crypto)
add_test(NAME auth COMMAND test_auth)

add_executable(test_watchdog tests/test_watchdog.cpp src/cat_sim.cpp src/radio.cpp)
add_test(NAME watchdog COMMAND test_watchdog)

add_executable(test_audio_queue tests/test_audio_queue.cpp src/audio.cpp src/http.cpp)
target_link_libraries(test_audio_queue PRIVATE civetweb-c-library)
add_test(NAME audio_queue COMMAND test_audio_queue)

# Drives the REAL serial code through a pty with a fake rig on the far end, so
# every serial failure mode is found without the station off the air.
add_executable(test_serial tests/test_serial.cpp src/serial_cat.cpp)
target_link_libraries(test_serial PRIVATE util)
add_test(NAME serial COMMAND test_serial)
