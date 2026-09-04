# The operating surface — worked out before another rewrite

**Written 09/04/2026, after Joe: *"the app just works but the workflow kinda sucks"* and
*"lets do this proper then work it out before anymore client rewrites/builds."***

Nothing in here has been built. This is the argument and the plan; code follows agreement.

---

## 1. What we have, as a number

**97 controls in 16 groups, in one scrolling column, at equal visual weight.**

| group | controls | | group | controls |
|---|---|---|---|---|
| Readout + meter | 21 | | Drive test | 3 |
| Band | 12 | | Levels | 4 |
| Mode | 6 | | Audio devices | 4 |
| VFO | 10 | | Recording | 2 |
| Tuning step | 5 | | Connection | 12 |
| Receiver | 13 | | Display | 1 |
| Antenna · Filter · RIT · Tuner | 11 | | Frequency entry | 14 |
| Transmit | 5 | | | |

Every one of them works. That is why "the app just works". The fault is that **the controls you
touch every over and the controls you set once a year have identical weight and live in the same
scroll.** There is no operating surface, so the operator hunts.

⚠️ **This is not a call to delete controls.** Remote operators lose access to the front panel;
the answer to "which do we cut" is usually "none, you cannot walk over and press it".

---

## 2. What operators actually complain about

Researched rather than assumed. Sources at the end.

| complaint | where | what it means for us |
|---|---|---|
| **Latency, and jitter more than latency** | measured remote work: ~26 ms local to ~250 ms at 6500 km, under 200 ms wanted; jitter is what actually breaks audio | The UI must show the LINK, not just the rig. A panel that looks identical on a healthy and a jittering link is lying by omission. |
| **UI and radio drift apart** | wfview: change frequency on the radio, the app does not follow | Everything shown must be read back FROM the radio. Already this repo's rule; the layout must not tempt anyone to break it. |
| **Settings you cannot reach remotely** | RS-BA1 reviews: menu items inaccessible, no manual frequency entry | The rig menus that matter remotely (MOD SOURCE, REAR SELECT, power) must be reachable — we learned this the hard way tonight. |
| **Steep setup** | RCForb: a 30-year IT professional and 32-year ham could not get it working | First run and reconnect must be trivial. Ours already remembers host/user; the connect screen is the only screen a new operator sees. |
| **"Make it look like a radio"** | RemoteHams reviews praise the interface but ask for a better rig-like skin | Our instrumentation look is an asset. Keep it; do not flatten it into a generic app. |
| **Accessibility is an afterthought** | Handiham's manual for blind hams: *"You cannot adjust any of the slider controls or change the CW speed or adjust many of the other knobs"*, and *"there does not appear any way of actually changing any of these setting with keyboard shortcuts"* | ⚠️ **The clearest open goal in this space.** Keyboard-complete operation is a design constraint from the start, not a retrofit. |
| **Stuck transmitters** | control-operator duty: must be able to terminate transmission immediately; timeouts and shutdown on link loss | The host already has the watchdog, the disconnect power reset and the MOD SOURCE restore. The UI half: a stop that is reachable from every screen, and a transmit state that is never in doubt. |

---

## 3. Principles

1. **Two surfaces: OPERATE and SETUP.** If you set it once a year, it is not on the operating
   surface. Nothing is removed; things move.
2. **Everything reads back from the radio.** No control shows its own click. (Repo rule already;
   §8g cost an evening to learn.)
3. **Transmit state is unambiguous, and stopping is always one action away** — from any screen,
   any tab, any window size.
4. **Show the link.** Round-trip, jitter, last-frame age, stale. Remote failure is usually link
   failure, and every other remote program leaves the operator guessing.
5. **Keyboard-complete.** Every operating action has a key, every control has a label a screen
   reader can read. This is where the field is weakest and where a small program can simply be
   better.
6. **One setting per thing.** One PTT key, not an in-app one and a global one (fixed 09/04).
7. **Density is a virtue, disorder is not.** A rig's front panel is dense and it is fine, because
   things are grouped where the hand expects them.

---

## 4. What NOT to do

- ⚠️ **No panadapter or waterfall.** The FTDX-101 gives no spectrum over CAT. Drawing one would
  be inventing data — the exact sin this repo already refuses for the ALC table and the S-meter
  scale.
- **No second version of a control.** One definition, shown in one or two places (PanelHead is
  the pattern).
- **No "simplified mode" that hides controls behind a preference.** That is the two-lists
  mistake again, one level up.

---

## 5. The proposed layout — DESKTOP

```
┌─ OPERATE ─── SETUP ──────────────────────────── link 41 ms · jitter 6 ms ─┐
│  40M                  7.195.000                    B 7.190               │
│  LSB      VFO A       100 W                        click to type ────────│
│  ▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇──────── S8    (drive/ALC/PWR/SWR when keyed)│
├──────────────────────────────────────────────────────────────────────────┤
│ 160  80  60  40  30  20  17  15  12  10  6      │  A   B   SWAP  SPLIT   │
│ LSB  USB  CW  AM  FM  DATA                      │  A▸B  QUICK  VFO LOCK  │
├─────────────────────────────────┬───────────────┴──────────────────────  │
│ NB   NR   NOTCH  ATT  PRE  AGC  │ NARROW  MED  WIDE     STEP 10 100 1k 5k │
│ MON  VOX  COMP   MUTE DIV  XIT  │ RIT  −  +  CLR        ANT 1  2  3       │
├─────────────────────────────────┴────────────────────────────────────────┤
│ [ ARM ]  [ ████ PTT ████ ]  [ TUNE ]     SWR 1.2   ALC 61%   PWR 78%     │
└──────────────────────────────────────────────────────────────────────────┘
```

- **The transmit bar is fixed**, on both tabs. Stopping is never behind a scroll (principle 3).
- **The link readout is in the title area**, always, on both tabs (principle 4).
- **Frequency entry is the readout itself** — click and type. The 14-key pad moves to a popover
  over the readout, which is where a hand looks for it, and stops eating a whole group.
- SETUP holds: audio devices, levels, recording, display/scale, PTT key, drive test, connection
  and disconnect, and the provenance notes.

## 6. The proposed layout — PHONE

Tonight's shape is close and stays: pinned head, tabs, one group, quick strip, ARM/PTT. Changes:

- the tab row gains **SETUP** as the last tab and loses nothing else;
- the link readout joins the pinned head, small, beside the band;
- FREQ becomes a popover on the readout rather than a tab, freeing a tab slot;
- the SET tab splits into **SETUP** (devices, levels, recording, display, PTT key) and the
  existing Connection rows.

## 7. Every control, classified

**OPERATE** — band(11), mode(6), VFO A/B/SWAP/SPLIT/A▸B/QUICK/LOCK(7), tuning step(5),
NB/NR/NOTCH/ATT/PRE/AGC/MON/VOX/COMP/MUTE/DIV/XIT(12), filter N/M/W(3), RIT −/+/CLR/RIT(4),
ANT 1/2/3(3), ARM/PTT/TUNE(3), the readout, the meters. **≈55 controls, all one click deep.**

**POPOVER** — the frequency keypad(14), on the readout.

**SETUP** — audio in/out devices(4), volume + mic gain(2), recording(2), display scale(1),
PTT key + hold/toggle(2), drive test(3), connection + disconnect(2), provenance text.
**≈16, none of them touched during a QSO.**

## 8. The five calls, made 09/04/2026

Joe: *"you can do what you need to and build the new clients hakld done sucks."* So these were
decided rather than left open, and each is one line to change:

1. **RIT is pinned** on the operating surface. Chasing a drifting station is a mid-QSO job.
2. **Mic gain is live on the operating surface**, in the transmit bar - setting drive means
   watching ALC while you move it, and Setup is the principled home and the wrong one.
3. **The link is a number AND a colour**: `link 41 ms · jitter 6 ms`, green/amber/red. The
   colour reads at a glance, the number is the evidence behind it. ⚠️ A stale rig outranks a
   fast link - 12 ms to a host that cannot hear the radio is not a healthy station.
4. **Drawn for 1280 and scaling up.** `--check-resolutions` walks 375 → 3840 either way.
5. **Nothing else moved to Setup** beyond the list in §7.

⚠️ **The keypad popover is NOT modal**, and that is principle 3 in one line: a modal popup greys
out the transmit bar, so with the keypad open the operator could not stop transmitting.

## 9. What the walk caught that reasoning had not

- the fixed-width transmit bar put a key **off the right edge** at minimum width on a 1440p
  screen - and the key that goes off the edge of a transmit bar is the one somebody needs in a
  hurry. It now gives way in a fixed order: the mic gain first, then the status text, then the
  labels shorten, and PTT is the last thing to shrink;
- the bar was **transparent**, so the panel scrolled visibly underneath the transmit keys;
- the KEYPAD key, anchored inside the readout, **landed on top of the POWER reading** at phone
  widths - the value still drawn, underneath a key.

## 10. Still open after this

- **Accessibility (P5).** The keyboard path exists for PTT and the panel is labelled, but
  "keyboard-complete" is not yet true and it is the field's clearest open goal. Next.
- The RX level is measured and shown as a number; it wants a proper meter beside the S-meter.

## 11. Original open questions (kept for the record)

1. **RIT** — pinned on the operating surface, or is it rare enough for a popover?
2. **Mic gain** — SETUP is the principled home, but you may want it live on the operating surface
   while watching ALC. Your call; it is one row either way.
3. **The link readout** — round-trip and jitter, or just a green/amber dot? The numbers are
   honest but they are also two more numbers on the screen.
4. **Windows/Linux window size** — is the panel usually maximised, or a window beside a logger?
   That decides whether OPERATE targets 1280 wide or 1920.
5. **Anything you reach for that is NOT in the OPERATE list above.**

---

## Sources

- Handiham, *RemoteHams User's Manual for Blind Hams* — https://handiham.org/remotebase/remotehams-users-manual-for-blind-hams/
- KU7T, *Ham radio remote operations latency measurements* — https://ku7t.org/ham-radio-remote-operations-latency-measurements/
- wfview forum, *Comparison with RS-BA1* — https://forum.wfview.org/t/comparison-with-rs-ba1/1189
- wfview forum, *Possible UI bug in the RADIO ACCESS section* — https://forum.wfview.org/t/possible-ui-bug-in-the-radio-access-section/4742
- eHam reviews, *RemoteHams remote control software* — https://www.eham.net/reviews/view-product?id=11983
- eHam reviews, *Icom RS-BA1* — https://www.eham.net/reviews/view-product?id=9940
- WA7RF, *Remote Ham Shacks and Remote Operations* — https://vhfclub.org/pdf/WA7RF%20Remote%20Operations.pdf
