cmake_minimum_required(VERSION 3.22)
project(hamdeck-client VERSION 0.1.15 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Multimedia Quick QuickControls2 Qml Test)
qt_standard_project_setup()

# ⚠️ ONE DEFINITION OF THE MAC APP NAME. It is the .app directory name, the executable
# name inside it, CFBundleName, and the install destination of the licence texts - four
# places that must agree or the bundle is subtly broken in a way only a Mac shows.
set(MACOS_APP_NAME "HamDeck Remote")

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

# ⚠️ The frequency parser, run against the REFERENCE implementation's own test
# cases (HamDeck.Tests/FreqInputTests.cs). A port that gives different answers
# to the same input is not a port, and the grouped "7.185.000" form the readout
# displays is exactly what a fresh implementation gets wrong.
qt_add_executable(test_knob tests_knob.cpp resources.qrc)
target_link_libraries(test_knob PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_test(NAME knob COMMAND test_knob)
set_tests_properties(knob PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_settings tests_settings.cpp src/settings.cpp)
target_link_libraries(test_settings PRIVATE Qt6::Core Qt6::Gui)
add_test(NAME settings COMMAND test_settings)

qt_add_executable(test_pcm tests_pcm.cpp src/pcm_convert.cpp)
target_link_libraries(test_pcm PRIVATE Qt6::Core)
add_test(NAME pcm COMMAND test_pcm)

qt_add_executable(test_freq tests_freq.cpp src/freq_input.cpp)
target_include_directories(test_freq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_freq PRIVATE Qt6::Core)
add_test(NAME freq COMMAND test_freq)
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
    src/freq_input.cpp
    src/global_hotkey.cpp
    src/settings.cpp
    src/api_client.cpp
    src/rx_audio.cpp
    src/tx_audio.cpp
    src/pcm_convert.cpp
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

# ── The macOS bundle: its name, its icon, and its microphone string ──────────
# ⚠️ EVERYTHING VISIBLE ABOUT A MAC APP COMES FROM THE BUNDLE, NOT THE BINARY.
# Left alone, CMake names the bundle after the TARGET and writes a stock Info.plist
# with no icon key, so 0.1.29 shipped a signed, notarised, correctly working app that
# Finder called "hamdeck-qml" and drew with the blank generic page. Nothing failed;
# there is simply no default that could have been right.
#
# ⚠️ OUTPUT_NAME IS WHAT RENAMES THE .app - the target stays hamdeck-qml so add_test,
# target_sources and every non-Apple path are untouched. It applies on APPLE only:
# Linux ships an executable called hamdeck-qml that a .desktop file points at, and a
# binary with a space in its name there would be a gratuitous break.
if(APPLE)
    set_target_properties(hamdeck-qml PROPERTIES
        OUTPUT_NAME                          "${MACOS_APP_NAME}"
        MACOSX_BUNDLE_INFO_PLIST             "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME            "${MACOS_APP_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER         "com.wa0o.hamdeck.remote"
        MACOSX_BUNDLE_BUNDLE_VERSION         "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT              "WA0O"
        MACOSX_BUNDLE_ICON_FILE              "hamdeck.icns")

    # ⚠️ THE ICNS IS A SOURCE FILE, NOT AN install(FILES). CFBundleIconFile names a file
    # that must already be in Contents/Resources of the BUILT bundle - macdeployqt,
    # codesign and notarisation all run against client/build before anything is
    # installed, so an icon added at install time is signed into nothing and the .app in
    # the DMG has no icon anyway. MACOSX_PACKAGE_LOCATION puts it there at build time.
    set(HAMDECK_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/icons/hamdeck.icns")
    if(NOT EXISTS "${HAMDECK_ICNS}")
        message(FATAL_ERROR "packaging/icons/hamdeck.icns is missing - run brand/build.sh")
    endif()
    target_sources(hamdeck-qml PRIVATE "${HAMDECK_ICNS}")
    set_source_files_properties("${HAMDECK_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# ⚠️ Embeds the .ico INTO the exe. Explorer, the taskbar and every shortcut read
# the icon from the binary's own resource - the installer's SetupIconFile does
# not give the installed app an icon, which is why it shipped blank.
# ⚠️ enable_language(RC) IS LOAD-BEARING. project() above declares LANGUAGES CXX,
# and without RC enabled CMake treats a .rc as a file it does not know how to
# compile and SILENTLY IGNORES IT - no warning, build succeeds, no icon. 0.1.12
# shipped exactly that: the .rc was committed, referenced here, and never
# compiled once. A green Windows build proved nothing.
if(WIN32)
    enable_language(RC)
    target_sources(hamdeck-qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/hamdeck.rc)
endif()

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

# ⚠️ macOS NEEDS A BUNDLE DESTINATION, AND WITHOUT IT CONFIGURE FAILS OUTRIGHT.
#
# qt_add_executable produces a MACOSX_BUNDLE on Apple, and install(TARGETS) refuses a
# bundle target that is only given RUNTIME DESTINATION:
#
#   install TARGETS given no BUNDLE DESTINATION for MACOSX_BUNDLE executable target
#
# So the macOS build did not merely fail to package - it never CONFIGURED. That went
# unnoticed for months because the macOS CI job pointed at a runner label GitHub had
# retired, so it queued forever instead of failing, behind continue-on-error and no
# upload. Two silent failures stacked on each other.
if(APPLE)
    install(TARGETS hamdeck-qml BUNDLE DESTINATION .)
else()
    install(TARGETS hamdeck-qml RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # ⚠️ freedesktop files, so Linux only. A .desktop entry and hicolor icons mean
    # nothing inside an .app bundle or on Windows.
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/hamdeck-client.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(SZ 16 24 32 48 64 128 256 512)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/icons/hamdeck-${SZ}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${SZ}x${SZ}/apps
                RENAME hamdeck.png)
    endforeach()
endif()

# ⚠️ Qt is LGPLv3 here, so it is linked DYNAMICALLY and the licence texts ship
# with the package. The bundled typefaces are SIL OFL, which also requires its
# licence to travel with any binary that embeds them.
# ⚠️ THE LICENCES MUST TRAVEL WITH THE BINARY ON EVERY PLATFORM, including macOS - SIL
# OFL requires it of anything that embeds the fonts, and a .app that leaves them behind
# is a licence problem rather than a packaging preference. Inside a bundle that means
# Resources/, not a Unix docdir.
if(APPLE)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/OFL.txt
            DESTINATION "${MACOS_APP_NAME}.app/Contents/Resources" RENAME OFL-fonts.txt)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/README.md
            DESTINATION "${MACOS_APP_NAME}.app/Contents/Resources" RENAME FONTS.md)
else()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/OFL.txt
            DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME OFL-fonts.txt)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/fonts/README.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME FONTS.md)
endif()

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
