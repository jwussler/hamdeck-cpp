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

static void EqStr(const char* what, const QString& got, const QString& want) {
    const bool ok = got == want;
    std::printf("  %s %s%s\n", ok ? "ok  " : "FAIL", what,
                ok ? "" : (" got \"" + got.toStdString() +
                           "\" wanted \"" + want.toStdString() + "\"").c_str());
    if (!ok) ++failures;
}

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

    // ── The target the operator typed ─────────────────────────────────────────
    // ⚠️ THESE EXIST BECAUSE A WRONG SCHEME IS SILENT. `ws://` to a TLS origin
    // gets a 400 from the proxy: the panel logs in, shows live status over REST,
    // and is deaf - no receive audio and no transmit - with nothing on screen
    // that points at the URL.
    {
        Settings::Target t = Settings::ParseTarget("192.168.40.64", 5002);
        EqStr("a bare address stays itself", t.host, "192.168.40.64");
        EqInt("a bare address uses the port field", t.port, 5002);
        EqInt("a bare address is not TLS", t.tls ? 1 : 0, 0);
    }
    {
        Settings::Target t = Settings::ParseTarget("https://radio.wa0o.com", 5002);
        EqStr("a scheme is stripped from the host", t.host, "radio.wa0o.com");
        EqInt("https means 443, not the stale port field", t.port, 443);
        EqInt("https means TLS", t.tls ? 1 : 0, 1);
    }
    {
        // What a browser's address bar hands over.
        Settings::Target t = Settings::ParseTarget("https://radio.wa0o.com/", 5002);
        EqStr("a trailing path is not part of the host", t.host, "radio.wa0o.com");
        EqInt("a pasted URL still means 443", t.port, 443);
    }
    {
        Settings::Target t = Settings::ParseTarget("http://192.168.40.64:5002", 443);
        EqStr("an explicit http scheme is stripped too", t.host, "192.168.40.64");
        EqInt("a port in the host beats the port field", t.port, 5002);
        EqInt("http means no TLS", t.tls ? 1 : 0, 0);
    }
    {
        Settings::Target t = Settings::ParseTarget("https://radio.wa0o.com:8443", 5002);
        EqInt("a port in the host beats the https default", t.port, 8443);
        EqInt("and it is still TLS", t.tls ? 1 : 0, 1);
    }
    {
        Settings::Target t = Settings::ParseTarget("  radio.wa0o.com  ", 0);
        EqStr("whitespace is trimmed", t.host, "radio.wa0o.com");
        EqInt("a zero port field falls back to 5002", t.port, 5002);
    }

    // ── The URLs built from it ────────────────────────────────────────────────
    {
        Settings s;
        s.host = "192.168.40.64"; s.port = 5002; s.tls = false;
        EqStr("plain base url", s.BaseUrl(), "http://192.168.40.64:5002");
        EqStr("plain socket url", s.WsUrl("/ws", "abc"),
              "ws://192.168.40.64:5002/ws?token=abc");
        s.host = "radio.wa0o.com"; s.port = 443; s.tls = true;
        EqStr("TLS base url", s.BaseUrl(), "https://radio.wa0o.com:443");
        EqStr("TLS receive socket follows the scheme", s.WsUrl("/ws", "abc"),
              "wss://radio.wa0o.com:443/ws?token=abc");
        EqStr("TLS transmit socket follows the scheme", s.WsUrl("/ws/tx", "abc"),
              "wss://radio.wa0o.com:443/ws/tx?token=abc");
    }

    // ⚠️ TLS IS PER-STATION, so it must survive a restart like host and port.
    {
        Settings s;
        s.Load();
        s.host = "radio.wa0o.com"; s.port = 443; s.tls = true;
        s.Save();
        Settings b;
        b.Load();
        EqInt("TLS survives a restart", b.tls ? 1 : 0, 1);
        EqInt("its port survives with it", b.port, 443);
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
