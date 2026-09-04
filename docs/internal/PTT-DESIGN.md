# The PTT key, walked through before it is written

**Joe, 09/04/2026: "lets walk that thru and see what you come up with."** He runs **Windows and
macOS**. Nothing below is built yet.

---

## 1. What it has to do

Key down keys the transmitter, key up unkeys it, **whether or not HamDeck has focus** - the
operator is looking at a logger, a cluster, or nothing at all. And it must never leave a carrier
up, which is the one failure that matters and the reason this document exists.

---

## 2. The mechanisms, and what they can really do

| | for the DOWN edge | for the UP edge | permission |
|---|---|---|---|
| **Windows** | `RegisterHotKey` + `MOD_NOREPEAT` | ⚠️ **`GetAsyncKeyState` polling** | none |
| **macOS** | Carbon `RegisterEventHotKey` | `kEventHotKeyReleased` — **thin** | none |
| **Linux** | X11 grab, fights the desktop | — | — |

⚠️ **THE FINDING, AND IT REVERSES WHAT WAS ASSUMED TWICE.** This repo has said since the first
commit that a Windows global hotkey "gives key-DOWN only, so it can only ever be press-to-toggle"
and that hold-to-talk needs a `WH_KEYBOARD_LL` hook that sees every keystroke on the machine.
**That is not the only option.** `RegisterHotKey` gives the down edge; `GetAsyncKeyState(vk)`
then answers "is that ONE key still down?" on a 25 ms timer. Down edge plus a state poll is
hold-to-talk, system-wide, **with no hook and no keylogging surface** - the poll can only ever
learn about the single key the operator nominated.

So **Windows gets hold-to-talk**, and the low-level hook stays refused for the reason it always
was.

⚠️ **And macOS is now the UNCERTAIN one, which is the opposite of yesterday's note.**
`RegisterEventHotKey` needs no permission and does deliver a release event - but
`kEventHotKeyReleased` is thinly documented and its delivery is not well attested. The obvious
mirror of the Windows trick, `CGEventSourceKeyState`, **may require Accessibility or Input
Monitoring on Big Sur and later** - reports conflict - and a PTT key that opens a permission
dialog on first press is not shippable. So macOS is: try the release event, **prove it on his
Mac**, and fall back honestly if it does not arrive.

---

## 3. Failure modes, and which layer catches each

The whole design is here. ⚠️ **Every row that ends in "carrier stays up" is the same incident.**

| what goes wrong | caught by | where it lives |
|---|---|---|
| release event never arrives (macOS) | key-state re-check on a timer where permitted; otherwise the hold limit below | client |
| the app is killed mid-hold | **the `/ws/tx` close → power cap + MOD SOURCE back to MIC** | host, already built |
| the link drops mid-hold | host's dead-link unkey | host, already built |
| the operator's machine sleeps mid-hold | hold limit, then the host watchdog | client, then host |
| an over genuinely runs long | ⚠️ **nothing must catch this** - see the hold limit | — |
| key auto-repeat flaps the rig | `MOD_NOREPEAT` / Carbon fires once / the existing suppression | client, already built |
| another app eats the key first (macOS) | press COUNT shown in the panel - a key that never fires reads as zero | client |
| registration refused (key already taken) | the status line says WHICH mode is armed | client |
| two clients both holding the claim | the panel says another client is connected | host + client, planned |

⚠️ **THE HOLD LIMIT IS THE HARD ONE, AND IT MUST NOT BE CLEVER.** A lost release and a long
transmission look identical from here: audio still streams, the link is fine, the operator is
simply talking. Anything that cuts an over short to protect against a lost release will
eventually cut somebody off mid-sentence on the air, which is worse than the bug.

So: **the client's hold limit is deliberately LONGER than any real over and SHORTER than the
host watchdog** - 150 s against the host's 180 s. It exists only so the operator is told *by
this app* which of the two safeguards fired, and the host's watchdog remains the thing that
actually protects the transmitter. Neither number is a guess about how long people talk; the
watchdog was already 180 s.

---

## 4. The ladder, and the status line that never lies

Three states, tried in order, and **the panel always names the one that is armed**:

1. **system-wide, hold** — Windows always; macOS if the release event proves out
2. **system-wide, toggle** — macOS if it does not; press to key, press to unkey
3. **focused only, hold** — registration refused (another app owns the key)

⚠️ A PTT that silently becomes a latch is a stuck transmitter waiting to happen, so the words
"hold" and "toggle" appear in the panel and are never inferred by the operator.

⚠️ **`FocusLost()` MEANS SOMETHING DIFFERENT NOW.** Today the client unkeys when the window
loses focus, because a key held through a focus change never delivers its release. Under a
system-wide hold that behaviour is exactly wrong - looking at the logger while transmitting is
the entire point. So: unkey on focus loss **only in state 3**.

---

## 5. ⚠️ One consequence that is Joe's call, not mine

**A single key registered system-wide is taken from every other application on that machine.**
Choose F9 and F9 stops working in the logger, the browser, everything, for as long as HamDeck is
running. That is fine for a key nothing else wants and a problem for a key something does.

- **F13/F14/F15** — no physical keyboard sends them, so nothing collides. Needs a footswitch or
  a programmable key mapped to it. **The right answer for anyone who has the hardware.**
- **Pause/Break, Scroll Lock** — pressable today, almost nothing else uses them.
- **F9, F12** — pressable, and commonly bound. ⚠️ Taking these globally WILL break them elsewhere.

**Decision needed:** the app will warn, at the moment the key is chosen, that it is about to be
taken from every other application - and it will name the app-wide consequence rather than
leaving it to be discovered. It will not refuse the choice; that is the operator's to make.

---

## 6. The gate, written before the code

1. **`tests_hotkey.cpp` extended** - the state machine: down keys, up unkeys, repeat suppressed,
   the hold limit fires, and a LOST release is simulated and shown to be caught.
2. **On his Windows box:** hold for 5 s with another app focused → the panel logs press and
   release with timestamps, and the rig follows.
3. **⚠️ On his Mac, and this decides the design:** the same test. If `kEventHotKeyReleased` does
   not arrive, macOS drops to toggle and the status line says so. **The claim "hold-to-talk
   works on macOS" is not made until that test is run** - same rule as a radio nobody here owns.

---

# Closing the window, and what keeps running

**Joe, 09/04/2026: "when we x the app we should minimise/reduce to the system tray or the
equivalent on the host OS"** — and, on the key: *"lets keep a selection on one option no need to
split it off"*, so there is ONE key selector and no second "global?" switch. System-wide is
attempted always; the status line names what was actually armed.

## One mechanism, both platforms

`QSystemTrayIcon` is the tray on Windows and becomes an **NSStatusItem in the menu bar** on
macOS, so one implementation covers both - which is the same "no need to split it off" answer.

| | closing the window does | quitting is |
|---|---|---|
| **Windows** | hides to the tray, app keeps running | tray menu → Quit |
| **macOS** | hides the window; the app stays in the Dock and the menu bar, which is the platform convention | ⌘Q |
| **Linux** | ⚠️ tray may not exist (`isSystemTrayAvailable()` false on a bare GNOME) - then X **quits**, and the panel says so rather than vanishing into nothing |

## ⚠️ THE HAZARD, AND IT IS THE WHOLE DESIGN

**A hidden HamDeck still holds the transmitter.** Close the window and the `/ws/tx` socket, the
single-transmitter claim and `MOD SOURCE=REAR` all persist - which is correct, because keying
from the logger is the entire point of a system-wide PTT. But it means the operator can close
the window believing they have finished, walk to the radio, and find **the hand mic dead**
because the rig is still on REAR for a client they cannot see.

So, three rules, none of them optional:

1. **The tray icon carries the state, not just the app.** Idle, armed, and ON AIR are three
   different icons, and ON AIR is unmistakable at 16 px. An app that can hold a transmitter
   while hidden must say so from where it is hiding.
2. **The tooltip says it in words** - "HamDeck — ARMED, radio on REAR/USB" - because an icon
   alone is a colour somebody has to remember the meaning of.
3. **Quit always releases.** Disarm, unkey, close `/ws/tx`; the host's close handler then
   restores the power cap and puts MOD SOURCE back to MIC. ⚠️ This must survive being killed
   too, which it already does - that safeguard lives next to the radio for exactly this reason.

⚠️ **And the first hide says so.** A one-time notification - "HamDeck is still running in the
tray, and still holds the transmitter" - because an app that vanishes silently is one the
operator assumes has stopped.

## What it does NOT do

- **It does not disarm on hide.** That would make background PTT useless, which is the feature
  the tray exists to serve.
- **It does not start hidden.** An app that has never shown itself, holding a transmitter, is a
  worse version of the same hazard.
