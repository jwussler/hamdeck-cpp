cmake_minimum_required(VERSION 3.22)
project(hamdeck-client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ⚠️ Qt is LGPLv3 here. That is fine for this repo, but it constrains
# DISTRIBUTION: link Qt dynamically (the default below) and ship the licence
# texts. Statically linking Qt without a commercial licence is the mistake to
# avoid, and it is one you only find out about at release time.
#
# Ubuntu 24.04 ships Qt 6.4, which is NOT an LTS. For something maintained for
# years, build releases against a Qt LTS (6.8) rather than whatever the distro
# happens to carry. 6.4 is fine to develop against.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Multimedia)
qt_standard_project_setup()

qt_add_executable(hamdeck-client
    src/main.cpp
    src/settings.cpp
    src/api_client.cpp
    src/rx_audio.cpp
    src/tx_audio.cpp
    src/ptt_hotkey.cpp
    src/mainwindow.cpp
    src/smeter.cpp
    src/theme.cpp
)

target_link_libraries(hamdeck-client PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia)

target_compile_options(hamdeck-client PRIVATE -Wall -Wextra)

# CI must RUN the binary, not just build it. The .NET client shipped a release
# that could not launch at all while every test passed.
enable_testing()
qt_add_executable(test_hotkey tests_hotkey.cpp src/ptt_hotkey.cpp)
target_link_libraries(test_hotkey PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME hotkey COMMAND test_hotkey)
set_tests_properties(hotkey PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME client_selftest COMMAND hamdeck-client --selftest)
set_tests_properties(client_selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
