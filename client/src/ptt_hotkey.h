#pragma once

// PTT hotkey.
//
// ⚠️ WHAT THIS DOES AND DOES NOT DO, STATED PLAINLY.
//
// This is an APPLICATION hotkey: it fires while the HamDeck window has focus.
// A *global* hotkey - one that works while another app is focused - cannot be
// done with Qt alone and needs platform code:
//
//   Windows: RegisterHotKey gives key-DOWN only, so it can only ever be
//            press-to-toggle. Hold-to-talk needs a WH_KEYBOARD_LL hook, which
//            sees every keystroke on the machine - a real privacy and
//            antivirus-flagging consideration, not just an implementation
//            detail.
//   X11:     XGrabKey, per-keycode, and it fights with desktop environments
//            that have already grabbed the combination.
//   macOS:   needs Accessibility permission, which the user must grant by hand.
//
// None of that is written yet. Saying "global hotkey" when it only works
// focused would be the same class of lie as a status route that reports ok for
// something it never did.
//
// ⚠️ AUTO-REPEAT MUST BE SUPPRESSED. A held key repeats at the OS repeat rate,
// and without filtering that flaps the transmitter on and off many times a
// second (docs/internal/CARRYOVER.md section 6 records this as mandatory on Windows via
// MOD_NOREPEAT). Handled here for every platform, not just Windows.

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>

class QKeyEvent;

// How the key behaves.
enum class PttMode {
  kHold,    // key down keys the rig, key up unkeys it. What most operators want.
  kToggle,  // press to key, press again to unkey. The only option a Windows
            // RegisterHotKey global hotkey can ever offer.
};

struct HotkeyChoice {
  const char* label;
  int qt_key;          // Qt::Key value
  const char* note;    // why you would or would not pick this
};

// Offered in the UI. Several, because any single choice can collide with
// something on a given machine - a keyboard driver, a game overlay, a desktop
// environment - and the fix should be "pick another" rather than "give up".
const QVector<HotkeyChoice>& PttHotkeyChoices();

class PttHotkey : public QObject {
  Q_OBJECT

 public:
  explicit PttHotkey(QObject* parent = nullptr);

  void SetKey(int qt_key) { key_ = qt_key; }
  void SetMode(PttMode m) { mode_ = m; }
  int key() const { return key_; }
  PttMode mode() const { return mode_; }

  // Feed key events here. Returns true if the event was consumed.
  bool HandleKeyPress(QKeyEvent* e);
  bool HandleKeyRelease(QKeyEvent* e);

  // Reset on focus loss: a key held when the window loses focus never delivers
  // its release, and the rig would stay keyed with the operator looking at
  // another window.
  void FocusLost();

  bool held() const { return held_; }

 signals:
  // true = key the rig, false = unkey it.
  void PttRequested(bool on);

 private:
  int key_ = 0;
  PttMode mode_ = PttMode::kHold;
  bool held_ = false;
  bool toggled_on_ = false;
};

QString DescribeKey(int qt_key);
