#pragma once

// The tray icon — and on macOS, the menu bar item.
//
// ⚠️ ONE MECHANISM FOR BOTH PLATFORMS. QSystemTrayIcon is the notification area
// on Windows and an NSStatusItem in the menu bar on macOS, so closing the window
// behaves like the platform expects without two implementations to keep in step.
//
// ⚠️ AND IT CARRIES THE TRANSMIT STATE, WHICH IS THE POINT. A hidden HamDeck
// still holds the /ws/tx socket, the host's single-transmitter claim and
// MOD SOURCE=REAR - correct, because keying from the logger is what a system-wide
// PTT is for, and dangerous, because the operator can close the window believing
// they have finished and then find the hand mic dead at the radio. An app that
// can hold a transmitter while hidden must say so from where it is hiding:
// three icons, and a tooltip that says it in words.

#include <QObject>
#include <QString>
#include <QSystemTrayIcon>
#include <memory>

class QMenu;

class Tray : public QObject {
  Q_OBJECT

 public:
  explicit Tray(QObject* parent = nullptr);
  ~Tray() override;

  // False when the desktop has no tray at all - a bare GNOME, a kiosk session.
  // ⚠️ The caller must then let the X button QUIT: an app that hides into
  // something that does not exist has vanished, and it is still holding a
  // transmitter.
  static bool Available();

  bool Install();          // false if there is no tray to install into
  void ShowFirstHideHint();

  // idle → armed → on air. Anything that can hold a transmitter while hidden
  // reports which of the three it is.
  void SetState(bool session, bool armed, bool tx, const QString& radio_note);

 signals:
  void showRequested();
  void quitRequested();

 private:
  void Repaint();

  std::unique_ptr<QSystemTrayIcon> icon_;
  QMenu* menu_ = nullptr;
  bool session_ = false, armed_ = false, tx_ = false;
  bool hinted_ = false;
  QString radio_note_;
};
