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

    // ── The portable profile ─────────────────────────────────────────────
    {
        Settings a;
        a.Load();
        a.mic_gain = 62;
        a.volume = 71;
        a.step_hz = 100;
        a.host = "station.example";        // machine-specific
        a.tx_device_name = "Some Mic On This PC";

        Settings b;
        b.Load();
        b.host = "other.example";
        b.tx_device_name = "A Different PC's Mic";
        b.ApplyProfileJson(a.ProfileJson());

        EqInt("mic gain follows the operator", b.mic_gain, 62);
        EqInt("volume follows the operator", b.volume, 71);
        EqInt("tuning step follows the operator", b.step_hz, 100);

        // ⚠️ Carrying a device NAME to another machine is how an operator ends
        // up armed against a microphone that does not exist there.
        const bool kept_device = b.tx_device_name == "A Different PC's Mic";
        std::printf("  %s the audio device stays machine-local%s\n",
                    kept_device ? "ok  " : "FAIL",
                    kept_device ? "" : ("  got " + b.tx_device_name.toStdString()).c_str());
        if (!kept_device) ++failures;

        const bool kept_host = b.host == "other.example";
        std::printf("  %s the host stays machine-local\n", kept_host ? "ok  " : "FAIL");
        if (!kept_host) ++failures;

        const bool no_secret = !a.ProfileJson().contains("password") &&
                               !a.ProfileJson().contains("token");
        std::printf("  %s the profile carries no credential\n", no_secret ? "ok  " : "FAIL");
        if (!no_secret) ++failures;
    }

    // ⚠️ A profile from an older client is MISSING keys a newer one knows. Absent
    // must mean "leave it alone", never zero - a mic gain of 0 is a dead
    // transmitter handed to the operator on login.
    {
        Settings s;
        s.Load();
        s.mic_gain = 62;
        s.volume = 71;
        s.ApplyProfileJson("{\"volume\":30}");
        EqInt("a key absent from the profile is left alone", s.mic_gain, 62);
        EqInt("a key present in the profile is applied", s.volume, 30);
    }

    // Junk must not wipe anything.
    {
        Settings s;
        s.Load();
        s.mic_gain = 62;
        s.ApplyProfileJson("not json at all");
        EqInt("junk leaves settings untouched", s.mic_gain, 62);
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
