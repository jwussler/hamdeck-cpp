#include "ptt_hotkey.h"

#include <QKeyEvent>

const QVector<HotkeyChoice>& PttHotkeyChoices() {
  // ⚠️ ORDERED BY WHAT SOMEBODY CAN ACTUALLY PRESS, not by what is cleanest.
  //
  // F13 is the *technically* ideal choice precisely because no physical keyboard
  // sends it - so nothing can collide with it. That is also exactly why it is
  // useless on its own: if no keyboard sends it, YOURS does not either. It needs
  // a footswitch, a macro key, or a programmable keyboard remapped to it.
  //
  // So the default is Pause/Break: present on most full-size keyboards, pressable
  // today, and almost nothing else listens for it. F13 is right there for anyone
  // who has the hardware for it, which is the setup most operators end up wanting.
  static const QVector<HotkeyChoice> kChoices = {
      {"Pause/Break", Qt::Key_Pause,
       "Default. On most full-size keyboards, and almost nothing else uses it. "
       "Pick this if you are pressing a key with your hand."},
      {"Scroll Lock", Qt::Key_ScrollLock,
       "Like Pause, and on many keyboards it lights an LED - a free transmit "
       "indicator. Missing from most compact keyboards."},
      {"F13", Qt::Key_F13,
       "The best choice IF you have the hardware for it. No physical keyboard "
       "sends F13, so nothing can conflict - and nothing can press it either. "
       "Map a footswitch or a programmable key to it."},
      {"F14", Qt::Key_F14, "As F13. Use if something already claims F13."},
      {"F15", Qt::Key_F15, "As F13. A third option for the same setup."},
      {"Right Ctrl", Qt::Key_Control,
       "Easy to reach and on every keyboard, but games, overlays and other apps "
       "commonly bind it."},
      {"F9", Qt::Key_F9,
       "Last resort: plenty of applications already bind F9."},
  };
  return kChoices;
}

QString DescribeKey(int qt_key) {
  for (const auto& c : PttHotkeyChoices()) {
    if (c.qt_key == qt_key) return c.label;
  }
  return QKeySequence(qt_key).toString();
}

PttHotkey::PttHotkey(QObject* parent) : QObject(parent) {}

bool PttHotkey::HandleKeyPress(QKeyEvent* e) {
  if (key_ == 0 || e->key() != key_) return false;

  // ⚠️ AUTO-REPEAT IS THE WHOLE POINT OF THIS BRANCH. A held key repeats at the
  // OS repeat rate; without this the transmitter is keyed and unkeyed several
  // times a second for as long as the operator holds the key.
  if (e->isAutoRepeat()) return true;

  if (mode_ == PttMode::kHold) {
    if (held_) return true;
    held_ = true;
    emit PttRequested(true);
  } else {
    toggled_on_ = !toggled_on_;
    emit PttRequested(toggled_on_);
  }
  return true;
}

bool PttHotkey::HandleKeyRelease(QKeyEvent* e) {
  if (key_ == 0 || e->key() != key_) return false;
  if (e->isAutoRepeat()) return true;

  if (mode_ == PttMode::kHold && held_) {
    held_ = false;
    emit PttRequested(false);
  }
  return true;
}

void PttHotkey::FocusLost() {
  // ⚠️ Unkey on focus loss. A key held when the window loses focus never
  // delivers its release event, so without this the rig stays keyed while the
  // operator is looking at a different application - and the only thing that
  // would stop it is the host watchdog, minutes later.
  if (held_) {
    held_ = false;
    emit PttRequested(false);
  }
  if (mode_ == PttMode::kToggle && toggled_on_) {
    toggled_on_ = false;
    emit PttRequested(false);
  }
}
