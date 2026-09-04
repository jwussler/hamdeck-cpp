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

// ⚠️ ONE KEY, ONE SETTING, BOTH SCOPES. There used to be two lists and two
// settings - an in-app key here and a separate "global PTT key" with its own
// labels - so a station could be set to F9 in one and F13 in the other and the
// operator had to hold both in their head to know what would key the rig.
// (Joe, 09/04/2026: "we just need one option to control ptt ... f9 for in app
// and f13 global is too confusing.")
//
// A choice now carries BOTH halves: the Qt key for the in-window filter, and the
// modifier/virtual-key pair the platform registration needs. Picking a key arms
// it everywhere it can be armed, and where the global registration fails the
// status line says so rather than the key quietly working in only one place.
//
// ⚠️ SINGLE KEYS ONLY, deliberately. Combinations (Ctrl+Alt+Space and friends)
// were on the global list and nowhere else, which is half of how the two lists
// drifted apart. A PTT key is pressed in a hurry, often without looking.
struct HotkeyChoice {
  const char* label;
  int qt_key;          // Qt::Key value, for the in-window event filter
  unsigned mods;       // platform modifier mask for the global registration
  unsigned vk;         // WINDOWS virtual-key for the global registration
  // ⚠️ macOS USES A DIFFERENT NUMBER FOR THE SAME KEY, and they are not
  // convertible - these are Carbon kVK_* codes. Zero means the key cannot be
  // registered system-wide on macOS at all, which is the honest answer for
  // Pause/Break and Scroll Lock (no Mac keyboard sends them) and for Right Ctrl
  // (RegisterEventHotKey cannot register a bare modifier). Those choices still
  // work window-focused there, and the status line says so.
  unsigned mac_vk;
  const char* note;    // why you would or would not pick this
};

// Offered in the UI. Several, because any single choice can collide with
// something on a given machine - a keyboard driver, a game overlay, a desktop
// environment - and the fix should be "pick another" rather than "give up".
const QVector<HotkeyChoice>& PttHotkeyChoices();

// Look one up by the label stored in settings. Null when the label is unknown -
// which happens to a profile written before this list existed.
const HotkeyChoice* PttHotkeyByLabel(const QString& label);

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
