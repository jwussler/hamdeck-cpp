#pragma once

// The iOS audio session. See ios_audio_session.mm for why a plist key alone does
// not keep audio playing in the background.
//
// On every other platform these are the two no-op stubs in main_qml.cpp's guard:
// nothing else in the client may reference them outside an #ifdef Q_OS_IOS.

#include <QString>

namespace ios_audio {
QString Configure();
QString State();
}  // namespace ios_audio
