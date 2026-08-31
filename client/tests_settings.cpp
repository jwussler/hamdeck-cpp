// Do the operator's settings actually survive a restart?
//
// ⚠️ MIC GAIN DID NOT. It was applied to the audio path and never written, so
// every restart silently returned it to 100% - the value that pins the rig's ALC
// and puts a splattering signal on the air. Nothing failed; the panel showed
// whatever was in memory, and the operator only found out by measuring their own
// transmitter.
//
// This round-trips a Settings through the real QSettings backing store the way a
// restart does: save, construct a fresh one, load, compare.

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

#include "src/settings.h"

static int failures = 0;

static void EqInt(const char* what, int got, int want) {
    const bool ok = got == want;
    std::printf("  %s %s%s\n", ok ? "ok  " : "FAIL", what,
                ok ? "" : (" got " + std::to_string(got) +
                           " wanted " + std::to_string(want)).c_str());
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("HamDeckClientTest");
    QCoreApplication::setOrganizationName("HamDeckTest");

    // ⚠️ Never touch the real config. A test that writes the operator's settings
    // could hand them a gain they did not choose.
    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        Settings s;
        s.Load();               // defaults
        EqInt("mic gain defaults to unity", s.mic_gain, 100);
        s.mic_gain = 55;
        s.volume = 42;
        s.Save();
    }
    {
        Settings s;             // a fresh one, as a restart would build
        s.Load();
        EqInt("mic gain survives a restart", s.mic_gain, 55);
        EqInt("volume survives a restart", s.volume, 42);
    }
    // The clamp lives in TxAudio::SetMicGain; what matters here is that whatever
    // value was stored comes back unchanged rather than silently reset.
    {
        Settings s;
        s.Load();
        s.mic_gain = 0;
        s.Save();
        Settings t;
        t.Load();
        EqInt("a gain of zero is stored, not treated as unset", t.mic_gain, 0);
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
