#pragma once

// A SYSTEM-WIDE PTT key: keys the rig while the panel is behind the logging
// program, which is where it spends most of a contest.
//
// Ported from HamDeck.Remote/GlobalHotkey.cs, including the parts that are not
// obvious from the API docs.
//
// ⚠️ PRESS TO TOGGLE, NOT HOLD, AND THAT IS A DELIBERATE LIMIT. Windows'
// RegisterHotKey reports key DOWN only - there is no key-up message - so true
// hold-to-talk needs a WH_KEYBOARD_LL hook, a global hook that sees every
// keystroke on the machine. That is an antivirus flag and a real privacy
// consideration, and it is a poor trade for something toggling covers. The
// window-focus hotkey (PttHotkey) still offers hold-to-talk; this does not.
//
// ⚠️ A TOGGLE CAN BE LEFT ON. The protection is the HOST watchdog next to the
// radio (ptt_timeout_seconds, 180 s), which unkeys even if this machine sleeps
// or loses the network. Nothing here substitutes for that.
//
// ⚠️ Alt+Space is deliberately not offered: it is the Windows system-menu
// accelerator, and taking it globally either fails or breaks that shortcut
// everywhere else.

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QWindow>

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT

 public:
  explicit GlobalHotkey(QObject* parent = nullptr);
  ~GlobalHotkey() override;

  // Labels offered in the UI, "Off" first. Mirrors the reference host's list.
  static QStringList Choices();

  // Register `label` for `window`. Returns an empty string on success, or WHY
  // it failed. ⚠️ A failure must be shown: a PTT key that silently does nothing
  // is worse than no PTT key, and the commonest cause - another program already
  // holds the combination - is not a fault in this app.
  QString Apply(const QString& label, QWindow* window);

  bool armed() const { return armed_; }
  QString label() const { return label_; }
  int pressCount() const { return press_count_; }

  bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

 signals:
  // One press of the registered key. Press-to-toggle is decided by the receiver.
  void Pressed();

 private:
  void Unregister();

  QString label_ = "Off";
  bool armed_ = false;
  bool installed_ = false;
  int press_count_ = 0;
  void* hwnd_ = nullptr;
};
