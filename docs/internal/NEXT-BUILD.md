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

**⚠️ I need from Joe:**
1. **Who is this for?** You alone, or public/blind operators too? Only the second one justifies
   full screen-reader semantics (an `Accessible` role and name on every control, tested with
   NVDA) rather than "just" a complete keyboard path. Different amount of work by a lot.
2. **Single keys or modified?** `b` for band up is fast and collides with typing; `Ctrl+B`
   never collides and is slower. My lean: single keys, suppressed whenever a field has focus.
3. **Which actions earn a key?** My proposal: band up/down, mode cycle, VFO A/B, swap, RIT
   clear, tune, mute, step up/down, and switching OPERATE/SETUP. Anything missing, anything
   you would not use?
4. **Focused or in the background?** If the client sits behind a logger, the keys have to be
   system-wide on Windows, which is a different mechanism with real limits (key-down only, so
   toggle rather than hold).

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

**⚠️ I need from Joe:**
1. **Should silence raise an alarm, and after how long?** 10 seconds of zero while connected is
   a dead receive path; 30 is safer against a genuinely quiet band. ⚠️ A quiet band and a broken
   path look identical to this meter - the alarm is a hint, never a diagnosis.
2. **How should it tell you?** Colour on the meter, a line in the panel, or a sound. A sound is
   the only one you would notice while looking away, and it is also the one that will annoy you
   first.

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
