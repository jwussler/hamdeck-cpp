cmake_minimum_required(VERSION 3.22)
project(hamdeck-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cpp-httplib, pinned. Header-only, no TLS (cloudflared terminates it upstream).
# NOTE: httplib has no WebSocket support, and the host needs /ws and /ws/tx.
# That framing will be hand-rolled or come from another library later - see WIP.md.
include(FetchContent)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(httplib)

find_package(OpenSSL REQUIRED)

add_executable(hamdeck-host src/main.cpp src/api.cpp src/cat_sim.cpp src/radio.cpp src/auth.cpp)
target_link_libraries(hamdeck-host PRIVATE httplib::httplib OpenSSL::Crypto)
target_compile_options(hamdeck-host PRIVATE -Wall -Wextra)

# CI must RUN things, not just build them (CARRYOVER.md section 8): the .NET
# client shipped a release that could not launch while every test passed.
enable_testing()
add_executable(test_staleness tests/test_staleness.cpp src/cat_sim.cpp src/radio.cpp)
add_test(NAME staleness COMMAND test_staleness)

add_executable(test_auth tests/test_auth.cpp src/auth.cpp)
target_link_libraries(test_auth PRIVATE OpenSSL::Crypto)
add_test(NAME auth COMMAND test_auth)
