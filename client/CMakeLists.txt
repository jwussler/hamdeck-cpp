cmake_minimum_required(VERSION 3.22)
project(hamdeck-client VERSION 0.1.3 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Multimedia Quick QuickControls2 Qml)
qt_standard_project_setup()

# ⚠️ The Qt Widgets front end was REMOVED here, not kept as a fallback.
#
# It worked and was screenshot-verified, but two front ends against one API
# drift - and the first thing to drift is exactly what a brand token table
# exists to prevent. The QML one is the decision (CLAUDE.md); git has the other
# if it is ever wanted back.
#
# What survived the swap is the part that mattered: api_client, rx_audio,
# tx_audio, ptt_hotkey and settings were never touched, because every safety
# rule lives there rather than in a view.

# CI must RUN the binary, not just build it. The .NET client shipped a release
# that could not launch at all while every test passed.
enable_testing()
qt_add_executable(test_hotkey tests_hotkey.cpp src/ptt_hotkey.cpp)
target_link_libraries(test_hotkey PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME hotkey COMMAND test_hotkey)

# ⚠️ Window placement, as its own test with no window in it. The offscreen QPA
# has no decoration and no window manager, so it cannot see the mistake this
# covers: x/y position the CLIENT area, and a window asked for (0,0) puts its
# title bar above the top of the screen. 0.1.2 shipped exactly that.
qt_add_executable(test_place tests_place.cpp src/place.cpp)
target_include_directories(test_place PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_place PRIVATE Qt6::Core)
add_test(NAME place COMMAND test_place)
set_tests_properties(hotkey PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The selftest that CI runs is qml_selftest, below.

# ── Qt Quick client ─────────────────────────────────────────────────────────
# QML rather than Widgets, per CLAUDE.md. The C++ core - api_client, rx_audio,
# tx_audio, ptt_hotkey, settings - is shared with the Widgets build unchanged,
# which is what made the port cheap: only the view was rewritten.
qt_add_executable(hamdeck-qml
    src/main_qml.cpp
    src/backend.cpp
    src/place.cpp
    src/settings.cpp
    src/api_client.cpp
    src/rx_audio.cpp
    src/tx_audio.cpp
    src/ptt_hotkey.cpp
    resources.qrc
)
target_link_libraries(hamdeck-qml PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::WebSockets Qt6::Multimedia
    Qt6::Quick Qt6::QuickControls2 Qt6::Qml)
# ⚠️ MSVC does not understand GCC/Clang warning flags and treats -Wextra as an
# invalid numeric argument, failing the whole build. Guard them by compiler.
# ⚠️ GUI SUBSYSTEM ON WINDOWS.
# Without WIN32_EXECUTABLE the linker produces a CONSOLE application, and
# Windows opens a black cmd window beside the panel on every launch. It is
# invisible when the only testing is headless on Linux, and the first thing an
# operator sees on Windows.
#
# The app still prints for --selftest and --screenshot: on Windows it attaches
# to the parent console when a command-line flag is present (see main_qml.cpp),
# so it behaves as a GUI app when double-clicked and as a console tool when run
# from a terminal. CI depends on that output.
set_target_properties(hamdeck-qml PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

if(MSVC)
    target_compile_options(hamdeck-qml PRIVATE /W3)
else()
    target_compile_options(hamdeck-qml PRIVATE -Wall -Wextra)
endif()

add_test(NAME qml_selftest COMMAND hamdeck-qml --selftest)
set_tests_properties(qml_selftest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

# ── Install and packaging ───────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS hamdeck-qml RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/hamdeck-client.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
foreach(SZ 16 24 32 48 64 128 256 512)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/icons/hamdeck-${SZ}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${SZ}x${SZ}/apps
            RENAME hamdeck.png)
endforeach()

# ⚠️ Qt is LGPLv3 here, so it is linked DYNAMICALLY and the licence texts ship
# with the package. The bundled typefaces are SIL OFL, which also requires its
# licence to travel with any binary that embeds them.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/OFL.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME OFL-fonts.txt)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME FONTS.md)

set(CPACK_PACKAGE_NAME "hamdeck-client")
set(CPACK_PACKAGE_DESCRIPTION
    "A radio front panel on a screen. Talks to a HamDeck host over the network -\n\
CAT control, receiver audio and transmit audio - and holds no rig state of its\n\
own. Pre-release: tested against one radio, and the installers are unsigned.")
# ⚠️ Declared, not bundled: Qt is LGPLv3 and dynamic linking is what keeps that
# obligation satisfied. The QML modules are separate packages and the app will
# start and then fail to load its UI without them, so they are hard dependencies.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6, libqt6gui6, libqt6network6, libqt6qml6, libqt6quick6, \
libqt6websockets6, libqt6multimedia6, qml6-module-qtqml, \
qml6-module-qtqml-workerscript, qml6-module-qtquick, \
qml6-module-qtquick-controls, qml6-module-qtquick-layouts, \
qml6-module-qtquick-templates, qml6-module-qtquick-window")
# .deb packaging is Linux-only. Including it elsewhere configures a generator
# that cannot run and clutters the build with irrelevant warnings.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../packaging/CPack.cmake)
    include(CPack)
endif()
