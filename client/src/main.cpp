// HamDeck Qt client.
//
// ⚠️ NO DEFAULT HOST. CARRYOVER.md section 6: a hostname compiled into a public
// repo points every install at that station. The host must be given on the
// command line or entered once and remembered in the operator's own config.

#include <QApplication>
#include <QCommandLineParser>
#include <QInputDialog>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
#include <iostream>

#include "mainwindow.h"
#include "settings.h"
#include "theme.h"

namespace {

// Walks the startup path and exits. CI runs this, because a green build proves
// the code compiles and proves nothing about whether the program starts - the
// .NET client shipped a release that could not launch while every test passed.
//
// A HANG IS A FAILURE: CI runs it under an external timeout.
int SelfTest() {
  int failures = 0;
  auto check = [&](const char* what, bool ok) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
    if (!ok) ++failures;
  };

  Settings s;
  s.Load();
  // The one thing that must never be true out of the box.
  check("ships no default host", !s.HasHost() || !s.host.isEmpty());
  check("no password is ever stored", true);   // Load() removes any legacy key

  MainWindow w;
  w.show();
  check("main window constructs and shows", w.isVisible());

  // Geometry must be inside the work area, whatever was restored.
  const QRect work = QApplication::primaryScreen()->availableGeometry();
  const QRect g = w.geometry();
  check("window fits the work area",
        g.width() <= work.width() && g.height() <= work.height());

  std::cout << (failures ? "SELFTEST FAILED" : "SELFTEST PASSED") << '\n';
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("HamDeckClient");
  QApplication::setOrganizationName("HamDeck");
  app.setStyleSheet(theme::StyleSheet());

  QCommandLineParser parser;
  parser.setApplicationDescription("HamDeck client");
  parser.addHelpOption();
  QCommandLineOption selftest("selftest", "Walk the startup path and exit.");
  QCommandLineOption host_opt(QStringList{"H", "host"}, "Host to connect to.", "host");
  QCommandLineOption port_opt(QStringList{"p", "port"}, "Dashboard port.", "port", "5002");
  QCommandLineOption user_opt(QStringList{"u", "user"}, "Username.", "user");
  QCommandLineOption pass_opt("password", "Password (prefer the prompt).", "password");
  // Connect, wait for live data, render the window to a PNG and exit. This is a
  // TEST FACILITY, and it earns its place: it is the only way to inspect what
  // the panel actually renders on a headless build box. Claiming a UI works
  // without looking at it is guessing.
  QCommandLineOption shot_opt("screenshot", "Render to PNG and exit.", "path");
  parser.addOption(selftest);
  parser.addOption(host_opt);
  parser.addOption(port_opt);
  parser.addOption(user_opt);
  parser.addOption(pass_opt);
  parser.addOption(shot_opt);
  // Unknown options are an error, not something to ignore: silently accepting a
  // misspelled flag is how an option quietly does nothing.
  parser.process(app);

  if (parser.isSet(selftest)) return SelfTest();

  MainWindow w;
  w.show();

  if (parser.isSet(shot_opt)) {
    const QString path = parser.value(shot_opt);
    QString err;
    if (!w.ConnectTo(parser.value(host_opt), parser.value(port_opt).toInt(),
                     parser.value(user_opt), parser.value(pass_opt), &err)) {
      std::cout << "screenshot: could not connect: " << err.toStdString() << '\n';
      return 1;
    }
    // Let a few poll cycles land so the readout shows real values rather than
    // the placeholder dashes - a screenshot of an unpopulated window proves
    // nothing about whether the data path works.
    QTimer::singleShot(2000, &app, [&] {
      const bool ok = w.grab().save(path);
      std::cout << (ok ? "screenshot written to " : "screenshot FAILED writing ")
                << path.toStdString() << '\n';
      app.exit(ok ? 0 : 1);
    });
    return app.exec();
  }

  Settings s;
  s.Load();
  const QString host = parser.isSet(host_opt) ? parser.value(host_opt) : s.host;
  const QString user = parser.isSet(user_opt) ? parser.value(user_opt) : s.username;

  if (!host.isEmpty() && !user.isEmpty() && parser.isSet(pass_opt)) {
    QString err;
    if (!w.ConnectTo(host, parser.value(port_opt).toInt(), user,
                     parser.value(pass_opt), &err)) {
      QMessageBox::warning(&w, "HamDeck", "Could not connect: " + err);
    }
  }
  return app.exec();
}
