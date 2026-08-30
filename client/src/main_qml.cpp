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
#include <QEventLoop>
#include <QPointF>
#include <functional>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
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
            // ⚠️ The panel is the thing that must fit, not the window. QML
            // honours a layout's minimum size hint, so a panel wider than the
            // window forces the window wider - which is how a title bar ends up
            // off-screen. It is also what --check-resolutions measures, so a
            // rename that loses the objectName must fail here rather than turn
            // that walk into a test of nothing.
            check("panel column found (--check-resolutions measures it)",
                  w->findChild<QQuickItem*>("panelColumn") != nullptr);
        }
    }

    const QStringList families = QFontDatabase::families();
    check("Barlow Condensed present", families.contains("Barlow Condensed"));
    check("IBM Plex Mono present", families.contains("IBM Plex Mono"));
    std::cout << (failures ? "SELFTEST FAILED" : "SELFTEST PASSED") << '\n';
    return failures ? 1 : 0;
}

// ⚠️ RESOLUTION IS A THING YOU MEASURE, NOT A THING YOU BELIEVE.
//
// Walks the panel across the screen sizes it will actually meet and, for each,
// measures THE KEYS - the things an operator has to hit and read:
//
//   - is any key drawn NARROWER THAN ITS OWN MINIMUM? A layout that runs out of
//     room squeezes its children, and a squeezed key clips its legend. This is
//     what a row that refuses to wrap looks like from the outside.
//   - is any key OFF THE RIGHT EDGE of the window? Horizontal scrolling on an
//     instrument panel means keys are simply not there.
//   - what is the smallest key, in pixels? A 30 px key is present, fits, and is
//     still not usable, so the number is printed rather than only judged.
//
// ⚠️ THE FIRST VERSION OF THIS MEASURED THE PANEL COLUMN'S implicitWidth AND
// ALWAYS REPORTED 20 px. The groups anchor their contents, which stops implicit
// width propagating, so it was reading the margins and passing every time - the
// section-6 trap (an estimate whose failure mode is a small number looks exactly
// like a working measurement) in a test written to avoid it. Hence the key
// count below: no keys found is a FAILURE, not a clean sheet.
// Run the event loop for a bit. Layout, resize delivery and polish all happen
// on posted events; nothing measured before they are drained is real.
void Pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int CheckResolutions(QQmlApplicationEngine& engine, Backend& backend, const QString& dir) {
    struct Size { int w, h; const char* what; };
    static const Size kSizes[] = {
        {1024,  600, "small netbook"},
        {1280,  720, "720p"},
        {1366,  768, "the commonest laptop"},
        {1600,  900, "laptop"},
        {1920, 1080, "1080p desktop"},
        {2560, 1440, "1440p"},
        {3840, 2160, "4K"},
    };

    auto* w = engine.rootObjects().isEmpty()
                  ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!w) { std::cout << "  FAIL no window\n"; return 1; }

    // ⚠️ A test that cannot fail is not a test. Without a session the window
    // shows the CONNECT screen, which fits every resolution ever made and would
    // report a clean pass while measuring nothing.
    if (!backend.sessionActive()) {
        std::cout << "  FAIL no session - this would measure the connect screen, "
                     "not the panel\n"
                     "RESOLUTION CHECK FAILED\n";
        return 1;
    }
    QQuickItem* panel = w->findChild<QQuickItem*>("panelColumn");
    if (!panel) {
        std::cout << "  FAIL panelColumn not found\n";
        return 1;
    }

    int failures = 0;
    std::cout << "  screen        window   panel    scale   keys  min key  squeezed  off-edge\n";
    for (const Size& s : kSizes) {
        // Two windows per screen: filling the work area, and the smallest the
        // app allows. The narrow one is where a row that will not wrap shows up.
        const int widths[2] = {s.w, static_cast<int>(w->minimumWidth())};
        for (int i = 0; i < 2; ++i) {
            backend.setScreen(s.w, s.h, 1.0);
            w->setWidth(widths[i]);
            w->setHeight(i == 0 ? s.h : qMin(s.h, static_cast<int>(w->minimumHeight())));
            // ⚠️ THE RESIZE IS NOT DONE WHEN setWidth RETURNS. It travels to the
            // QML side as a posted event, and grabWindow renders without
            // processing the queue - so the first version of this walk measured
            // a 459 px panel inside a 1920 px window and blamed the layout. Spin
            // the loop until the geometry has actually arrived.
            Pump(120);
            const QImage img = w->grabWindow();
            // Report the window as the WINDOW says it is, not as we asked. A
            // resize the platform declined must be visible, not assumed.
            const int win_w = static_cast<int>(w->width());

            int keys = 0, squeezed = 0, off_edge = 0;
            double min_w = 1e9;
            QStringList offenders;

            // ⚠️ THE VISUAL TREE, NOT findChildren(). QObject parentage misses
            // every item a Repeater created - which is the band row, the mode
            // row, the receiver row and the keypad, i.e. most of the panel.
            // findChildren found 17 of about 60 keys and reported a pass on
            // them: a measurement that counts only what you thought to look for
            // is not coverage.
            std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
                for (QQuickItem* it : item->childItems()) {
                    if (it->objectName() == QLatin1String("panelKey") &&
                        it->isVisible() && it->width() > 0) {
                        ++keys;
                        min_w = qMin(min_w, it->width());
                        const bool sq = it->width() < it->implicitWidth() - 0.5;
                        const double right = it->mapToScene(QPointF(it->width(), 0)).x();
                        const bool off = right > w->width() + 1;
                        if (sq) ++squeezed;
                        if (off) ++off_edge;
                        // Name what failed. A count says a row is wrong; the
                        // legend says WHICH row, which is the difference
                        // between a finding and a mystery.
                        if ((sq || off) && offenders.size() < 6) {
                            offenders << it->property("text").toString() +
                                         (off ? "(off)" : "(squeezed)");
                        }
                    }
                    visit(it);
                }
            };
            visit(w->contentItem());
            // No keys means the walk measured an empty or unpopulated panel.
            const bool ok = keys > 0 && squeezed == 0 && off_edge == 0;
            if (!ok) ++failures;
            char line[200];
            std::snprintf(line, sizeof(line),
                          "  %4dx%-4d  %5dpx  %5.0fpx  %5.2fx  %5d  %6.0fpx  %8d  %8d  %s",
                          s.w, s.h, win_w, panel->width(), backend.uiScale(), keys,
                          keys ? min_w : 0.0, squeezed, off_edge, ok ? "ok" : "FAIL");
            std::cout << line << "  " << (i == 0 ? s.what : "at minimum width") << '\n';
            if (!offenders.isEmpty())
                std::cout << "      " << offenders.join(", ").toStdString() << '\n';
            if (!dir.isEmpty() && i == 0) {
                const QString path = QString("%1/panel-%2x%3.png").arg(dir).arg(s.w).arg(s.h);
                if (!img.save(path)) std::cout << "    (could not write " << path.toStdString() << ")\n";
            }
        }
    }
    std::cout << (failures ? "RESOLUTION CHECK FAILED\n" : "RESOLUTION CHECK PASSED\n");
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
    QCommandLineOption scale_opt("ui-scale", "Force the UI scale (0.5-3.0).", "factor");
    QCommandLineOption shotsize_opt("screenshot-size", "Screenshot size, WxH.", "WxH");
    QCommandLineOption reset_opt("reset-window",
                                "Forget the saved window position and exit.");
    QCommandLineOption res_opt("check-resolutions",
                               "Walk the panel across screen sizes; PNGs into DIR.", "dir");
    for (auto* o : {&selftest, &host_opt, &port_opt, &user_opt, &pass_opt, &shot_opt, &tone_opt,
                    &scale_opt, &shotsize_opt, &res_opt, &reset_opt}) {
        parser.addOption(*o);
    }
    parser.process(app);   // unknown options abort

    Backend backend;
    // ⚠️ Before the QML loads, and it exits without opening a window: the whole
    // point is to be usable when the window cannot be reached.
    if (parser.isSet(reset_opt)) {
        backend.resetWindowGeometry();
        std::cout << "saved window position cleared - start HamDeck normally\n";
        return 0;
    }
    if (parser.isSet(tone_opt)) backend.useTestTone();
    // ⚠️ Set BEFORE the QML loads. Applied afterwards it would be a visible
    // relayout, and a screenshot could catch the panel mid-change.
    if (parser.isSet(scale_opt)) {
        bool ok = false;
        const double f = parser.value(scale_opt).toDouble(&ok);
        if (!ok || f <= 0) {
            std::cerr << "FATAL: --ui-scale wants a number, got '"
                      << parser.value(scale_opt).toStdString() << "'\n";
            return 2;
        }
        backend.setUiScaleOverride(f);
    }

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

    if (parser.isSet(res_opt)) {
        // Needs live data for the same reason a screenshot does: an empty panel
        // lays out differently from one with a frequency and a meter in it.
        QTimer::singleShot(2500, &app, [&] {
            const int rc = CheckResolutions(engine, backend, parser.value(res_opt));
            backend.shutdown();
            app.exit(rc);
        });
        return app.exec();
    }

    if (parser.isSet(shot_opt)) {
        const QString path = parser.value(shot_opt);
        auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (parser.isSet(tone_opt)) QTimer::singleShot(700, &app, [&] { backend.toggleArm(); });
        // Grow to the whole panel for capture only. Normal operation keeps the
        // work-area clamp, which exists to stop exactly this on a real desktop.
        int shot_w = 920, shot_h = 1180;
        if (parser.isSet(shotsize_opt)) {
            const QStringList wh = parser.value(shotsize_opt).split('x', Qt::SkipEmptyParts);
            bool ok = wh.size() == 2;
            if (ok) { shot_w = wh[0].toInt(&ok); }
            if (ok) { shot_h = wh[1].toInt(&ok); }
            if (!ok || shot_w < 200 || shot_h < 200) {
                std::cerr << "FATAL: --screenshot-size wants WxH, got '"
                          << parser.value(shotsize_opt).toStdString() << "'\n";
                return 2;
            }
            // The scale follows the screen, so a capture at a size must be told
            // that is the screen - otherwise it is this box's monitor drawn into
            // somebody else's window size, which is a picture of nothing.
            backend.setScreen(shot_w, shot_h, 1.0);
        }
        if (w) { w->setWidth(shot_w); w->setHeight(shot_h); }
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
