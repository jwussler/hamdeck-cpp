# Next build — walked out before it is written

**Joe, 09/04/2026: "lets walk out what you need before we do another build."** So this exists
before the code does. Rule: `walk-it-out-before-building`.

Two pieces are outstanding. For each: what it must do, what I am deciding, **what I need from
Joe**, and the gate that will prove it.

---

## A. Keyboard-complete operation

**Why:** the clearest open goal in this field. Handiham's manual for blind hams says of the
best-known remote client: *"you cannot adjust any of the slider controls… there does not appear
any way of actually changing any of these setting with keyboard shortcuts."* Ours has a PTT key
and nothing else. Designing it in now costs a fraction of retrofitting it.

**What it must do:** every operating action reachable from the keyboard, a visible focus ring,
and a key map the operator can see without leaving the panel.

**I decide:** focus ring styling, tab order, the help overlay, and how keys are suppressed while
a text field has focus.

**ANSWERED 09/04/2026.** *"for me now but i would really like to have this take off and go
somewhere also"* and *"i run windows and mac os"*.

1. **Built for him, aimed at others.** Every control gets an accessible name and role AS IT IS
   WRITTEN - nearly free in the file, expensive as a retrofit. ⚠️ **But it ships labelled and
   keyboard-complete, NOT verified**: that needs NVDA or VoiceOver and a real screen-reader
   user. Documented as *believed working, untested with a screen reader*, exactly like a radio
   nobody here owns.
2. **Single keys, dead while a text field has focus.** Fast to press, and the collision case is
   handled by where focus is rather than by a modifier on every key.
3. **The map:** band up/down, mode cycle, VFO A/B, swap, RIT clear, tune, mute, step up/down,
   OPERATE/SETUP. `?` shows it. All of it vetoable in one line each.
4. **⚠️ SYSTEM-WIDE, AND THE TWO PLATFORMS ARE NOT THE SAME. He runs both.**

   | | mechanism | what it can do | permission |
   |---|---|---|---|
   | **macOS** | Carbon `RegisterEventHotKey` | press AND release, so **hold-to-talk** | **none** |
   | **Windows** | `RegisterHotKey` | key-DOWN only, so **toggle** | none |
   | **Linux** | X11 grab, fights the desktop | focused only | — |

   ⚠️ **macOS is the better platform here and that is the opposite of what was assumed.**
   `RegisterEventHotKey` needs no Accessibility permission - it is narrowly scoped, the app
   only ever learns that one combination was pressed - and it is what VS Code, Slack and
   Electron use. Deprecated, stable, and the only public API that does this without a prompt.

   ⚠️ **BUT `kEventHotKeyReleased` IS THINLY DOCUMENTED.** The release half is what hold-to-talk
   depends on, and its delivery is not well attested. So: implement it, **prove it on his Mac
   before claiming it**, and if release does not arrive reliably, fall back to toggle on macOS
   and SAY SO in the status line. A PTT that silently becomes a latch is a stuck transmitter.
   The host's watchdog is the backstop, and the client unkeys on focus loss regardless.

   ⚠️ **The low-level keyboard hook is still refused on Windows** - it sees every keystroke on
   the machine, which is a privacy and antivirus problem, not an implementation detail. Toggle
   system-wide, hold when focused, and the status line says which is armed.

**Gate:** a test that drives the panel by synthetic key events only - no mouse - and asserts the
rig route fired for each one. `tests_knob.cpp` is the model: a control no test touches is a
control nobody has tested.

---

## B. The receive meter, and the no-audio alarm

**Why:** the host measures `rx_peak` now and the client shows it as a number in the transmit
bar. That answers "is audio flowing" only if somebody is looking at the right corner.

**What it must do:** show receive level beside the S-meter, and say so when it is zero while
connected - which is the exact condition that had us guessing tonight.

**I decide:** the meter's placement in the head and its scale.

**DECIDED 09/04/2026** - *"the others you can figure out and let me know the solution."*

1. **20 seconds of zero receive, while connected and NOT transmitting.** Ten is too twitchy on a
   dead band between overs; thirty is long enough to have already asked out loud what is wrong.
2. **Colour on the meter, and a line in the transmit bar** - which is on screen on both surfaces,
   so it is seen without looking for it. **No sound.** In a radio application a beep competes
   with the band audio the operator is straining to hear, and it would fire on every quiet
   moment on a dead band.
3. ⚠️ **It says what it measured, not what it means:** "no receive audio for 20 s". A quiet band
   and a broken path are identical to this meter, and a panel that announced "receive is broken"
   would be a confident wrong answer roughly as often as it was right.

**Gate:** extend `test_rx_peak` - it already proves the level falls and reads zero on silence -
with the alarm's threshold and its reset.

---

## C. Facts I need regardless

1. **Which desktop do you actually run the client on day to day** - Windows, or the Linux box?
   It decides where the keyboard work is proven, and whether the global-hotkey path matters.
2. **Do you ever run two clients at once** (desk and phone together)? The host allows it; the
   transmit claim does not, and the panel says nothing about the other one.
3. **Is anyone else running HamDeck yet**, or is it still just you? It changes how careful the
   migrations have to be when a default moves.
