#pragma once

// One HamDeck per machine, per user.
//
// ⚠️ THE TRAY CREATED THIS HAZARD AND THIS CLOSES IT. Closing the window now
// HIDES the app, and a hidden HamDeck still holds the /ws/tx socket, the host's
// single-transmitter claim and MOD SOURCE=REAR. So the obvious thing an operator
// does next - "the window is gone, I will start it again" - used to launch a
// SECOND client that fights the first for a claim only one of them can have,
// while the first sits invisible still holding the radio.
//
// Starting a second copy now raises the first one instead.
//
// ⚠️ NOT APPLIED TO THE HEADLESS MODES. --selftest, --check-resolutions and
// --drive-sweep run in CI, sometimes several at once on one runner, and a lock
// that made the second of them exit silently would turn a real failure into a
// green tick.

#include <QObject>
#include <QString>
#include <memory>

class QLocalServer;

class SingleInstance : public QObject {
  Q_OBJECT

 public:
  explicit SingleInstance(QObject* parent = nullptr);
  ~SingleInstance() override;

  // True if this process is the first one. False means another copy is running
  // and has been asked to show itself - the caller should exit immediately.
  bool Claim();

 signals:
  // Another copy tried to start. Bring the window back rather than leaving the
  // operator thinking nothing happened.
  void showRequested();

 private:
  std::unique_ptr<QLocalServer> server_;
};
