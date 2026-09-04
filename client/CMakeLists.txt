cmake_minimum_required(VERSION 3.22)
# ⚠️ THE VERSION COMES FROM THE TAG, NOT FROM THIS FILE.
#
# It used to be hardcoded here, and it drifted: v0.1.29 shipped
# hamdeck-client_0.1.15_amd64.deb, fourteen versions behind the release it was
# built from, because the pusher took its version from the tag (vpk
# --packVersion) and this did not. The download looked stale while being current
# - and the same number goes into CFBundleShortVersionString, so the Mac app
# would have told Get Info it was 0.1.15 as well.
#
# The literal below is the fallback for a local build with no tag, and it is the
# only place it should ever appear. CI passes -DHAMDECK_VERSION=<tag without v>.
if(NOT DEFINED HAMDECK_VERSION OR HAMDECK_VERSION STREQUAL "")
    set(HAMDECK_VERSION "0.0.0")
endif()
project(hamdeck-client VERSION ${HAMDECK_VERSION} LANGUAGES CXX)

# ⚠️ The iOS bundle identifier is a BUILD INPUT, not a literal, because it has to
# match an App ID created in Apple's portal and a provisioning profile issued
# against it. Hardcoding one means a mismatch is discovered at signing time, with
# an error that talks about entitlements rather than about this string.
if(NOT DEFINED HAMDECK_IOS_BUNDLE_ID OR HAMDECK_IOS_BUNDLE_ID STREQUAL "")
    set(HAMDECK_IOS_BUNDLE_ID "com.wa0o.hamdeck")
endif()

# ⚠️ THE BUILD NUMBER IS SEPARATE FROM THE VERSION, because Apple treats them as
# two different things and only one of them may repeat. CFBundleShortVersionString
# is what a user sees (0.1.0); CFBundleVersion is the BUILD, and App Store Connect
# refuses a second upload that reuses one - including after a rejected delivery,
# where nothing is visible in the portal to explain what the collision is with.
# Defaults to the version so a local build needs nothing; CI passes it.
if(NOT DEFINED HAMDECK_IOS_BUILD OR HAMDECK_IOS_BUILD STREQUAL "")
    set(HAMDECK_IOS_BUILD "${HAMDECK_VERSION}")
endif()
message(STATUS "hamdeck-client version: ${PROJECT_VERSION}")

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
# ⚠️ NOT ON iOS. Every target below is a command-line test binary that CI runs on
# the build machine. Cross-compiled for a phone they build into app bundles that
# nothing can execute, and `ctest` then reports a suite that has never run as a
# suite that passed - the exact shape of "green and never exercised" this file
# exists to avoid.
if(NOT IOS)
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
endif()  # NOT IOS

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
# ⚠️ AND NOT IOS. iOS is APPLE too, and everything in this block is wrong there:
# an iOS bundle is flat (no Contents/), OUTPUT_NAME with a space fights the
# provisioning profile's expectation of the executable name, and LSMinimumSystemVersion
# is a macOS key that iOS ignores while the key it actually needs is missing.
if(APPLE AND NOT IOS)
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
# ── The iOS bundle ──────────────────────────────────────────────────────────
# ⚠️ THE EXECUTABLE KEEPS ITS TARGET NAME on iOS - no OUTPUT_NAME with a space.
# CFBundleExecutable, the provisioning profile and the signature all have to name
# the same file, and a space is the kind of difference that surfaces as an
# unhelpful codesign error at the very end of a long build.
#
# ⚠️ QT FOR iOS IS STATIC-ONLY. There is no shared build. Read docs/IOS.md before
# distributing anything built this way: the LGPL note at the top of this file is
# about exactly this, and iOS is the one platform where it is unavoidable.
# ⚠️ THE TRAY IS DESKTOP-ONLY, AND SO IS THE WIDGETS DEPENDENCY IT DRAGS IN.
# QSystemTrayIcon and QMenu live in QtWidgets and need a QApplication; a phone
# has no tray, no menu bar and no window to close to one. Compiling it there
# would add a whole Qt module to an app that could never use it.
if(NOT IOS)
    target_sources(hamdeck-qml PRIVATE src/tray.cpp)
    target_link_libraries(hamdeck-qml PRIVATE Qt6::Widgets)
endif()

if(IOS)
    # ⚠️ OBJECTIVE-C++ IS A SEPARATE LANGUAGE TO CMake, and a .mm added without
    # enabling it is compiled as C++ - which fails on the first @import with an
    # error about a stray '@' rather than about the language. The audio session
    # has to be Objective-C++ because AVAudioSession has no C API.
    enable_language(OBJCXX)
    target_sources(hamdeck-qml PRIVATE src/ios_audio_session.mm)
    # ⚠️ AVFoundation is not linked by Qt's iOS package for us. Without it the
    # build gets to the very last step and dies on undefined _OBJC_CLASS_$_
    # AVAudioSession, which reads as a missing source file.
    target_link_libraries(hamdeck-qml PRIVATE "-framework AVFoundation" "-framework Foundation")

    set_target_properties(hamdeck-qml PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST     "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME    "HamDeck"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${HAMDECK_IOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${HAMDECK_IOS_BUILD}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${HAMDECK_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
    message(STATUS "iOS bundle id: ${HAMDECK_IOS_BUNDLE_ID}, version ${PROJECT_VERSION}, build ${HAMDECK_IOS_BUILD}")

    # ⚠️ SIMULATOR ONLY, AND IT MUST STAY THAT WAY.
    #
    # Qt's iOS package ships the permission plugins' _init objects as DEVICE
    # slices only, so a simulator link dies with:
    #
    #   ld: building for 'iOS-simulator', but linking in object file
    #       (.../plugins/permissions/.../QDarwinMicrophonePermissionPlugin_init.cpp.o)
    #       built for 'iOS'
    #
    # Everything COMPILES; only the link fails, which is why this looks like a
    # code problem and is not one. Dropping the plugin type is harmless for a
    # smoke test - a simulator has no microphone to authorise - and it is WRONG
    # for a device build, where asking for the mic is the entire point of the
    # app. Hence the explicit flag rather than sniffing the sysroot: a build that
    # silently dropped the permission plugin would install on a phone, run, and
    # then fail to transmit with nothing to say why.
    # ── The app icon ────────────────────────────────────────────────────
    # ⚠️ AN ASSET CATALOG, NOT A LOOSE PNG. Since iOS 11 the App Store REQUIRES
    # icons to come from a compiled catalog, and the upload is where you find
    # out: the build compiles, links, signs and exports perfectly, then altool
    # rejects it with 90713 / 90022 / 90023. Nothing earlier in the chain looks
    # at icons at all.
    #
    # One 1024x1024 entry - Xcode derives every other size from it. The art is
    # FULL-BLEED and has NO ALPHA: iOS applies its own superellipse mask, so a
    # pre-rounded tile would be masked twice, and an icon with an alpha channel
    # is rejected outright.
    set(IOS_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios/Assets.xcassets")
    target_sources(hamdeck-qml PRIVATE "${IOS_ASSETS}")
    set_source_files_properties("${IOS_ASSETS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(hamdeck-qml PROPERTIES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")

    # ⚠️ NOT THE FFmpeg MEDIA BACKEND. Qt's iOS package ships
    # libQt6FFmpegMediaPluginImpl.a but NOT the FFmpeg libraries it calls, so a
    # static link ends in a wall of undefined _sws_* and _av_* symbols. Nothing
    # in this app decodes video; the plugin is pulled in purely because static
    # builds auto-import every backend.
    #
    # Apple platforms have a native backend (AVFoundation) which is the correct
    # one here regardless - it is what QAudioSource and QAudioSink use on a
    # phone, and it is the one that respects the microphone permission.
    qt_import_plugins(hamdeck-qml
        INCLUDE_BY_TYPE multimedia Qt6::QDarwinMediaPlugin
        EXCLUDE Qt6::QFFmpegMediaPlugin)

    if(HAMDECK_IOS_SIMULATOR)
        message(WARNING "iOS SIMULATOR build: permission plugins excluded. "
                        "This bundle cannot request the microphone - smoke test only.")
        qt_import_plugins(hamdeck-qml EXCLUDE_BY_TYPE permissions)
    endif()
endif()

if(WIN32)
    enable_language(RC)
    target_sources(hamdeck-qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/hamdeck.rc)
endif()

if(MSVC)
    target_compile_options(hamdeck-qml PRIVATE /W3)
else()
    target_compile_options(hamdeck-qml PRIVATE -Wall -Wextra)
endif()

# ⚠️ Guards the failure that cannot be reproduced on the machine that ships it:
# a system-wide PTT whose release event never arrives. See tests_hold_limit.cpp.
add_executable(test_hold_limit tests_hold_limit.cpp)
target_link_libraries(test_hold_limit PRIVATE Qt6::Core)
add_test(NAME hold_limit COMMAND test_hold_limit)

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
# ⚠️ AND NOT IOS: an iOS bundle is FLAT, so Contents/Resources does not exist
# there and install() would create a directory Apple's tooling does not expect.
# The fonts are already inside the binary via resources.qrc; on iOS the licence
# texts travel in the repo and the App Store listing rather than in the bundle.
if(APPLE AND NOT IOS)
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
