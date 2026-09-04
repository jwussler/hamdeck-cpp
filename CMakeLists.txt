cmake_minimum_required(VERSION 3.22)
# ⚠️ THE VERSION COMES FROM THE TAG, like every artifact this project ships.
# It was hardcoded 0.1.0 here while the clients were at 0.1.33, so a station box
# reported a number that said nothing about what it was running - the same drift
# that once put a 0.1.15 .deb inside a v0.1.29 release, on the half that nothing
# packaged. CI passes -DHAMDECK_VERSION=<tag without v>; the literal is only the
# fallback for a local build with no tag.
if(NOT DEFINED HAMDECK_VERSION OR HAMDECK_VERSION STREQUAL "")
    set(HAMDECK_VERSION "0.0.0")
endif()
project(hamdeck-cpp VERSION ${HAMDECK_VERSION} LANGUAGES CXX)
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
# ⚠️ EXCLUDE_FROM_ALL, OR ITS INSTALL RULES BECOME OURS. Without this the .deb
# shipped libcivetweb.a, civetweb's CMake target files and two pkgconfig files
# into /usr on the operator's machine - a vendored dependency's build artefacts
# installed system-wide, where they can collide with a real civetweb package.
# Caught by reading `dpkg-deb -c` on the first package ever built, not by the
# build, which was perfectly happy.
FetchContent_MakeAvailable(civetweb)
set_property(DIRECTORY ${civetweb_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)

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
# Header-only, but it installs its headers and cmake config the same way.
set_property(DIRECTORY ${nlohmann_json_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)

add_executable(hamdeck-host src/main.cpp src/admin_page.cpp src/log.cpp src/amp_tuner.cpp src/api.cpp src/transmit_routes.cpp src/cw_text.cpp src/http.cpp src/audio.cpp src/cat_sim.cpp src/serial_cat.cpp src/radio.cpp src/auth.cpp src/config.cpp src/tx_audio.cpp src/cat_proxy.cpp src/alsa_devices.cpp src/rig_cal.cpp src/alsa_audio.cpp src/tgxl.cpp src/recorder.cpp src/qso_record.cpp
    src/session_stats.cpp)
target_link_libraries(hamdeck-host PRIVATE civetweb-c-library OpenSSL::Crypto nlohmann_json::nlohmann_json asound)
target_compile_options(hamdeck-host PRIVATE -Wall -Wextra)

# CI must RUN things, not just build them (docs/internal/CARRYOVER.md section 8): the .NET
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

add_executable(test_rx_peak tests/test_rx_peak.cpp src/audio.cpp src/http.cpp src/recorder.cpp)
target_link_libraries(test_rx_peak PRIVATE civetweb-c-library)
add_test(NAME rx_peak COMMAND test_rx_peak)

add_executable(test_recorder tests/test_recorder.cpp src/recorder.cpp)
add_test(NAME recorder COMMAND test_recorder)

add_executable(test_qso_record tests/test_qso_record.cpp src/qso_record.cpp src/recorder.cpp)
add_test(NAME qso_record COMMAND test_qso_record)

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

# ⚠️ Drives the REAL binary over HTTP, because the amp gate's failure mode was a
# refusal served as HTTP 200 - which every unit test and every route inventory
# read as success while the Stream Deck button sat dead. Needs the host built.
add_test(NAME amp_gate COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/amp_gate_check.sh
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(amp_gate PROPERTIES DEPENDS transmit_gate)

add_executable(test_remote_active tests/test_remote_active.cpp src/auth.cpp)
target_link_libraries(test_remote_active PRIVATE OpenSSL::Crypto)
add_test(NAME remote_active COMMAND test_remote_active)

add_executable(test_admin tests/test_admin.cpp src/auth.cpp)
target_link_libraries(test_admin PRIVATE OpenSSL::Crypto)
add_test(NAME admin COMMAND test_admin)

# ══ Packaging the HOST ══════════════════════════════════════════════════════
#
# ⚠️ THE HOST WAS NEVER PACKAGED AT ALL, AND THAT IS THE PRODUCT'S MISSING HALF.
# Joe, 09/04/2026: "a 1/2 assed product without a server but a great client
# sucks." Until now the only way this reached a machine was rsyncing the source
# to a box and building it there - so anybody who downloaded HamDeck got a panel
# with nothing to connect to.
#
# ⚠️ /opt/hamdeck-cpp, NOT /usr/bin, and hamdeck-cpp.service, NOT hamdeck-host.
# Both are FHS-unfashionable and both are deliberate: the running station already
# has that path and that unit name, so this package UPGRADES it in place instead
# of leaving a second copy and a second service fighting over one radio.
install(TARGETS hamdeck-host RUNTIME DESTINATION /opt/hamdeck-cpp)

install(FILES deploy/hamdeck-cpp.service
              deploy/hamdeck-rig-watchdog.service
              deploy/hamdeck-rig-watchdog.timer
        DESTINATION /lib/systemd/system)

install(PROGRAMS deploy/hamdeck-rig-watchdog DESTINATION /opt/hamdeck-cpp/tools)

# ⚠️ THE SETUP STEP AS A COMMAND, NOT A PARAGRAPH. Producing the admin
# credential meant reimplementing PBKDF2 outside this program - the exact
# "setup defeats experienced people" failure the research turned up, aimed at
# our own first five minutes.
install(PROGRAMS packaging/deb/hamdeck-host-password DESTINATION /usr/bin)

# ⚠️ THE UDEV RULE AND THE MODULE LIST ARE NOT OPTIONAL EXTRAS. The CP2105 CAT
# bridge and the PCM2903C codec need cp210x and snd_usb_audio, and a cloud or
# minimal kernel ships NEITHER - the host then starts, finds no radio, and looks
# broken. The rule is what gives /dev/ttyRIG its stable name.
install(FILES deploy/99-hamdeck-radio.rules DESTINATION /lib/udev/rules.d)

# ⚠️ INSTALLED AS AN EXAMPLE, NEVER AS config.json. The live config holds the
# operator's users and is REWRITTEN by the host itself on every admin change;
# a package that overwrote it would delete their accounts on upgrade.
install(FILES deploy/config.example.json DESTINATION /etc/hamdeck-cpp)

set(CPACK_PACKAGE_NAME "hamdeck-host")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "WA0O")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "HamDeck host - CAT control, REST API and remote audio for a station radio")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
# Runtime only. The build needs OpenSSL headers; the installed binary needs the
# library, ALSA, and nothing else - civetweb and nlohmann/json are vendored in.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl3 | libssl3t64, libasound2 | libasound2t64")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/prerm")
include(CPack)
