// HamDeck Qt Quick client.
//
// QML rather than Widgets, per CLAUDE.md: the panel is custom-drawn dark
// instrumentation - meters, segment digits, lit keys - which QML is designed for
// and which Widgets fights, every custom element becoming a QPainter subclass.
//
// ⚠️ NO DEFAULT HOST. A hostname compiled into a public repo points every
// install at that station.

#include <QFontDatabase>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>

#include "backend.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

namespace {

#ifdef _WIN32
// ⚠️ A GUI-subsystem app has no console, so --selftest and --screenshot would
// print into nothing. Attaching to the PARENT console gives the best of both:
// double-clicked it is a silent GUI app with no stray cmd window, and run from
// a terminal it still reports. Called only when a flag was passed, so a normal
// launch never touches a console at all.
void AttachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}
#endif

// ⚠️ The brand faces are BUNDLED, not assumed present. BRAND.md makes the point
// about a logo that names a font by name and falls back to whatever the viewer
// happens to have; a panel rendered in a substitute face is not the panel that
// was designed.
void LoadBundledFonts() {
    const char* kFonts[] = {
        ":/fonts/BarlowCondensed-SemiBold.ttf",
        ":/fonts/BarlowCondensed-Bold.ttf",
        ":/fonts/IBMPlexSans.ttf",
        ":/fonts/IBMPlexMono-Regular.ttf",
        ":/fonts/IBMPlexMono-Medium.ttf",
    };
    for (const char* f : kFonts) {
        if (QFontDatabase::addApplicationFont(QString::fromUtf8(f)) < 0) {
            // Say so rather than rendering in a substitute and calling it done.
            std::cerr << "warning: could not load bundled font " << f << '\n';
        }
    }
}

int SelfTest(QQmlApplicationEngine& engine) {
    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
        if (!ok) ++failures;
    };
    check("QML loaded without errors", !engine.rootObjects().isEmpty());
    if (!engine.rootObjects().isEmpty()) {
        auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        check("root object is a window", w != nullptr);
        if (w) {
            check("window fits the work area",
                  w->width() <= QGuiApplication::primaryScreen()->availableSize().width() &&
                  w->height() <= QGuiApplication::primaryScreen()->availableSize().height());
        }
    }
    const QStringList families = QFontDatabase::families();
    check("Barlow Condensed present", families.contains("Barlow Condensed"));
    check("IBM Plex Mono present", families.contains("IBM Plex Mono"));
    std::cout << (failures ? "SELFTEST FAILED" : "SELFTEST PASSED") << '\n';
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Any flag means somebody launched this from a terminal and wants output.
    if (argc > 1) AttachParentConsole();
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("HamDeckClient");
    QGuiApplication::setOrganizationName("HamDeck");
    LoadBundledFonts();

    QCommandLineParser parser;
    parser.setApplicationDescription("HamDeck client");
    parser.addHelpOption();
    QCommandLineOption selftest("selftest", "Walk the startup path and exit.");
    QCommandLineOption host_opt(QStringList{"H", "host"}, "Host.", "host");
    QCommandLineOption port_opt(QStringList{"p", "port"}, "Dashboard port.", "port", "5002");
    QCommandLineOption user_opt(QStringList{"u", "user"}, "Username.", "user");
    QCommandLineOption pass_opt("password", "Password.", "password");
    QCommandLineOption shot_opt("screenshot", "Render to PNG and exit.", "path");
    QCommandLineOption tone_opt("tx-test-tone", "Transmit a test tone, not the microphone.");
    for (auto* o : {&selftest, &host_opt, &port_opt, &user_opt, &pass_opt, &shot_opt, &tone_opt}) {
        parser.addOption(*o);
    }
    parser.process(app);   // unknown options abort

    Backend backend;
    if (parser.isSet(tone_opt)) backend.useTestTone();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.addImportPath(":/qml");
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "FATAL: QML failed to load\n";
        return 1;
    }

    if (parser.isSet(selftest)) {
        const int rc = SelfTest(engine);
        backend.shutdown();
        return rc;
    }

    // Stop cleanly however the app ends, not just on the paths we remembered.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::shutdown);

    if (parser.isSet(host_opt) && parser.isSet(user_opt) && parser.isSet(pass_opt)) {
        backend.connectTo(parser.value(host_opt), parser.value(port_opt).toInt(),
                          parser.value(user_opt), parser.value(pass_opt));
    }

    if (parser.isSet(shot_opt)) {
        const QString path = parser.value(shot_opt);
        auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (parser.isSet(tone_opt)) QTimer::singleShot(700, &app, [&] { backend.toggleArm(); });
        // Grow to the whole panel for capture only. Normal operation keeps the
        // work-area clamp, which exists to stop exactly this on a real desktop.
        if (w) { w->setWidth(920); w->setHeight(1180); }
        // Let real poll data land: a screenshot of an unpopulated window proves
        // nothing about whether the data path works.
        QTimer::singleShot(parser.isSet(tone_opt) ? 5000 : 2500, &app, [&, w, path] {
            const bool ok = w && w->grabWindow().save(path);
            std::cout << (ok ? "screenshot written to " : "screenshot FAILED ")
                      << path.toStdString() << '\n';
            backend.shutdown();
            app.exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
