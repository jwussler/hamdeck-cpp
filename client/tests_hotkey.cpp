// PTT hotkey behaviour.
//
// The two assertions that matter are both about NOT leaving the rig keyed:
// auto-repeat must not flap the transmitter, and losing focus with the key held
// must unkey it.

#include <QApplication>
#include <QKeyEvent>
#include <cassert>
#include <cstdio>
#include <vector>

#include "src/ptt_hotkey.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  setvbuf(stdout, nullptr, _IONBF, 0);

  PttHotkey hk;
  hk.SetKey(Qt::Key_Pause);
  hk.SetMode(PttMode::kHold);

  std::vector<bool> events;
  QObject::connect(&hk, &PttHotkey::PttRequested,
                   [&](bool on) { events.push_back(on); });

  auto press = [&](bool autorep) {
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Pause, Qt::NoModifier, QString(), autorep);
    hk.HandleKeyPress(&e);
  };
  auto release = [&](bool autorep) {
    QKeyEvent e(QEvent::KeyRelease, Qt::Key_Pause, Qt::NoModifier, QString(), autorep);
    hk.HandleKeyRelease(&e);
  };

  // Hold: one key-down, a burst of auto-repeats, one key-up.
  press(false);
  for (int i = 0; i < 25; ++i) press(true);   // the OS repeating a held key
  release(false);

  // ⚠️ Exactly two events. Without auto-repeat suppression this would be ~52,
  // keying and unkeying the transmitter 25 times while the operator held one key.
  std::printf("hold:     1 press + 25 auto-repeats + 1 release -> %zu events\n",
              events.size());
  assert(events.size() == 2);
  assert(events[0] == true && events[1] == false);

  // A different key must be ignored entirely.
  events.clear();
  QKeyEvent other(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
  assert(!hk.HandleKeyPress(&other));
  assert(events.empty());
  std::printf("ignore:   an unrelated key produces nothing\n");

  // ⚠️ Focus loss while held must UNKEY. A key held when the window loses focus
  // never delivers its release, so the rig would stay keyed with the operator
  // looking at another window - and only the host watchdog would stop it.
  events.clear();
  press(false);
  assert(hk.held());
  hk.FocusLost();
  assert(!hk.held());
  assert(events.size() == 2 && events[1] == false);
  std::printf("focus:    key held + focus lost -> unkeyed, not left transmitting\n");

  // Toggle mode: press keys, press again unkeys, and focus loss still unkeys.
  events.clear();
  hk.SetMode(PttMode::kToggle);
  press(false); release(false);
  press(false); release(false);
  assert(events.size() == 2 && events[0] == true && events[1] == false);
  std::printf("toggle:   press keys, press again unkeys\n");

  events.clear();
  press(false); release(false);            // on
  hk.FocusLost();
  assert(events.size() == 2 && events[1] == false);
  std::printf("toggle:   focus loss unkeys a latched transmit too\n");

  std::printf("PASS\n");
  return 0;
}
