# HamDeck C++ — work in progress

Mid-build handover. Written 08/30/2026. Read §1 and §2 before touching anything.

> **THIS REPO IS PUBLIC.** Nothing station-specific goes in it — no hostnames, addresses, VM
> ids, tunnel details, or live readings that say what the station was doing. That applies to
> **commit messages** too. Site detail lives in the gitignored `SITE.md`.
> CARRYOVER.md §6 states the narrow version: *a hostname in a public repo points every install
> at that station.* It generalises.

---

## 1. Status at a glance

> **End of 08/30/2026: see `DAY-08-30-2026.md`** for the full day across all three
> repos, including the .NET and brand work that is not recorded here.

| | |
|---|---|
| **Host** | **running the station.** The .NET server VM is shut down |
| **Client** | QML panel: connects, live status, rig control, RX **and TX** audio, PTT hotkey, admin, **resolution-aware** (§8d) |
| **Route coverage** | **140 of 141** exact routes + all **18** prefix families (DX cluster only, which 404s on the reference too) |
| **Tests** | **11 host (ctest)**, all green — and now green under `RelWithDebInfo` too, which is what CI builds |
| **Parity** | 25/25 with 5 listed deliberate divergences |
| **Verification** | 12/12 read-only, 50/50 driven routes read back from the rig, 46/46 smoke, 7/7 honest-absence, keypad and VFO-lock walked |
| **Radio** | **attached and operating.** §4b records the six CAT bugs only hardware found |

### Deploying — use `tools/deploy.sh`, not `sync.sh`
⚠️ `sync.sh` builds on the VM and **installs nothing**. On 08/30 the service ran a binary
from hours earlier while new routes were being "verified" against it; they 404'd, which is
the only reason it was caught. `tools/deploy.sh` runs the suite, installs, restarts, and
then compares the build id the **running service** reports (`/api/build`) against the
binary it just installed. It is proven to fail on a stale binary.

### Backups — because there is no remote yet
`tools/backup.sh` bundles the whole repo, **verifies the bundle**, copies it to a second
machine and **compares checksums** — `scp` exiting 0 says the transfer ran, not that the bytes
arrived intact. It keeps the last 10 locally. Host and path come from
`HAMDECK_BACKUP_HOST` / `HAMDECK_BACKUP_PATH`; the values are in `SITE.md`.

Proven on 08/30/2026, not assumed: the bundle was pulled **back** from the second machine,
restored, built, and passed all 8 tests.

⚠️ **The bundle contains the full history, including the site detail still present in older
commits.** It is safe on private storage and **must not be published** — which is also why the
script has no GitHub path in it.

### History scrub — done 08/30/2026
The git history **was** carrying site detail (WAN address, VPN peer endpoint, internal
addresses, VM ids, ssh aliases) even though the working tree was clean. It was rewritten with
`git-filter-repo`, replacing in **both blobs and commit messages** — `--replace-text` alone
only touches blobs, which is why the first attempt still leaked.

Verified afterwards: no match for any of those patterns anywhere in blobs or messages, all 24
commits preserved, and the rewritten tree still builds and passes all 8 tests. The rewrite was
validated **on a clone first**; the real repo was only touched once the copy came out clean.

⚠️ **The pre-rewrite bundles in the backup location still contain the original history.** They
are private-storage-only and must never be published. See `SITE.md`.

### What is left, and what it needs

| work | needs the radio? |
|---|---|
| Real ALSA capture and playback (replacing the tone source and null sink) | **yes** |
| `/proc/asound` delay measurement + adaptive buffering | **yes** |
| ~~PTT tail-wait~~ ✅ **done and tested on the radio** — measured 149 ms drained | done |
| RX mute on TX is done client-side; host-side recording is not | **yes** |
| ~~Client TX audio~~ ✅ done — 93.8 KiB/s measured into the host | no |
| PTT hotkey ✅ done (window-focus). **Global** hotkey still needs platform code | no |
- **`/api/admin/*` user management** ✅ done — users, add, password, remove, transmit
  permission, sessions, kick, lockdown. The remaining admin routes (presets, flexknob
  buttons, mic release, rport gain, tx devices) are for hardware this host does not have.
| `/audio` HTTP endpoint, `/wsflexknob`, the CAT proxy, the static web UI | no |

`/api/cluster/spots` and `/api/session` are **absent on the reference host too** — matching
that is correct, not a gap.

---

## 2. Where the work happens

- Code lives on the workstation; it is **built and run on a disposable build VM**, which is
  also where the ALSA and serial work will run. That VM is the only place a green build means
  anything.
- A **reference host** runs the working .NET version against the real radio. It is **read
  from** for contracts and **never written to**.
- Site specifics — hostnames, addresses, VM ids, the hardware-window commands — are in the
  gitignored `SITE.md`.

```sh
HAMDECK_BUILD_HOST=<ssh target> ./sync.sh     # rsync + build on the VM
ssh <vm> 'cd ~/hamdeck-cpp/build && ctest'    # host tests
cmake -S client -B client/build -G Ninja      # the Qt client
QT_QPA_PLATFORM=offscreen ./hamdeck-client --selftest
```

`sync.sh` **ships no default host** and refuses to run without `HAMDECK_BUILD_HOST`.

### ⚠️ The rig hardware is single-instance
There is exactly one CAT bridge and one USB codec, and the **reference host holds both**. Only
one machine can have the radio at a time, so the build VM has **no USB passthrough at all** and
cannot take them by accident. The CAT bridge is a *dual* UART: one device enumerates two serial
ports and passing it moves both.

**The cutover gate is met** — the serial backend is written and passing against a pty. A
hardware window should now be spent on what only the radio can show. Order, so the risky part
is last:

1. `HAMDECK_CAT_DEVICE=auto ./hamdeck-host --selftest` — proves port, probe and identity in
   seconds, keying nothing.
2. `/api/status` against the real rig, compared with the reference host.
3. Only then the audio work.

Exact commands for taking and returning the radio are in `SITE.md`. **Never leave the radio
detached from the reference host unattended.**

---

## 3. Decisions already made — do not relitigate

- **No Authelia, no SSO, no added login step.** The host is reachable only over the VPN and has
  its own session auth. A remote IdP would add a failure mode, not a boundary.
- **The power cap is intentional**: a local caller is capped lower than a remote one. It reads
  backwards. It is deliberate. **Do not "fix" it.**
- **API-first, no privileged client.** Every capability reachable through a documented route;
  the browser UI would be just another consumer. The only asymmetry allowed is local vs
  remote, enforced by *which socket accepted the request*.
- **Qt + CMake for the desktop client.** The hard part is the audio path, which needs native
  audio; WPF cannot cross-compile, which is why the .NET client is Windows-only. A browser
  client cannot be the primary panel: `getUserMedia` needs a secure context, so the mic is
  blocked over plain HTTP on a LAN address, and a browser cannot hold a global PTT hotkey when
  unfocused. A browser PWA is still right for a *phone monitor*, once the hostname has TLS.
  ⚠️ Qt is LGPLv3 here: link it dynamically and ship the licence texts. Build releases against
  a Qt **LTS**, not whatever the distro carries.

---

## 4. Architecture

### Two listeners, and the split is load-bearing
| port | binding | behaviour |
|---|---|---|
| control | **loopback only** | no session required; a LAN caller is refused by the kernel |
| dashboard | LAN | only `/api/health` and `/api/auth/status` anonymous; everything else 401 |

This is what makes "local" mean something, and how `/api/tune/amp` refuses every remote caller.
Locality is **which socket accepted the request** — never a header, which the caller controls.

### Two gates, both before routing
1. **Auth**, defaulting to **DENY**. `/api/ptt/on` answered 401 before it existed, so every
   route added later is protected unless deliberately listed anonymous. Open-unless-remembered
   is the shape that fails open.
2. **The software VFO lock**, which *blocks* frequency-changing routes for every caller. An
   operator who locks the VFO has said *do not move my frequency*. It applies on both
   listeners — a local caller is trusted for auth, but the lock is not a permission level.

### One thread owns the serial port
The serial lock is not re-entrant across threads. **Request threads never touch it.** Handlers
`Enqueue()` a CAT command; the poller drains the queue at the top of each 200 ms cycle and
serves `/api/status` entirely from cache. Compound read-modify-write sequences (quick-split,
vfo-copy, remote-tx, step) go through `EnqueueTask()` and run *on* the poller thread, so a
status poll cannot land mid-sequence and cache a half-applied state.

Queueing a command marks the slow-polled set dirty so the next cycle **re-reads** it — it does
not assume the command worked, because an optimistic update lies whenever the rig rejects it.

### Safety that lives next to the radio
- **Transmit watchdog**, default 180 s. A client-side timeout protects nothing: close the tab
  or lose the link while keyed and the rig stays keyed.
- **Unkey on shutdown.** The watchdog lives *in this process*; if it exits while keyed nothing
  is left to drop PTT. Shutdown stops the listeners, stops the poller, then drops PTT and
  **confirms by reading `TX;` back**. Measured 2.3 s idle / 3.3 s keyed, inside the unit's
  10 s stop timeout — overrunning it would mean SIGKILL and no unkey.
- **Exclusive serial access**, failing closed. Two processes interleaving commands on one CAT
  link produce replies attributed to the wrong command.

### Audio
`/ws` RX 22050/16/mono, `/ws/tx` TX 48000/16/mono — the asymmetry is the codec's: capture does
8000–48000, playback only 32000–48000.

- **One writer thread**, per-connection locking, so frames cannot interleave on a socket.
- **RX queue bounded at 10, drop the OLDEST** — keeps a listener on live audio instead of an
  ever-later recording.
- **TX queue deliberately runs past its bound while keyed** — dropping mid-transmission removes
  a syllable from someone's sentence. It trims on unkey, so every over starts at the target
  depth however far the link drifted.
- **The auth gate runs on the WebSocket upgrade**, so audio is refused before a single frame.
- `/ws/tx` also requires **`can_transmit`** and a **single-transmitter claim**: a session is not
  enough, and two clients feeding the rig would interleave two voices into one carrier.

---

## 4b. What the hardware window found (08/30/2026)

The radio was moved to the build VM and the C++ host ran it for real. Everything below was
invisible against the simulator.

### Six bugs, all fixed
| what | the bug |
|---|---|
| compressor | `PR0P2` is **1=OFF, 2=ON**, not a flag. The toggle would have sent "off" for on and an invalid code for off. |
| AGC | codes **5 and 6 also mean AUTO**. The rig answered `GT06`; a switch knowing only 0-4 read AUTO by accident. |
| lock | **`LK4` is not locked** — only `LK1` is. Any-non-zero reported a lock that was not there. |
| reply slip | asking `ID;` returned **`VS0;ID0682;`** — a stale reply in front of the wanted one. Now the reply must start with the command's verb. |
| probe | opening a named port proved a port existed, not that a radio was on it. It probes with `ID;` now either way. |
| audio default | `alsa_*_device` defaulted to `"default"`, a **real device** on most systems. Empty now means synthetic. |

### TX buffering took three attempts, and the failures are the lesson
1. **Write each chunk on arrival** → 290 underruns in 6 s, stream stuck in `XRUN`.
2. **Hand-rolled pre-roll** → *worse*, 1512 underruns in 18 s. Feeding one chunk per cycle is
   exactly real time and never accumulates a cushion. **Pre-roll belongs to ALSA's
   `start_threshold`.**
3. **`start_threshold` set to the same 150 ms the feeder targets** → deadlock. The feeder
   stopped at 140 ms because one more chunk would overshoot, so the device never reached the
   150 ms it needed to start: a perfectly steady buffer, zero underruns, **total silence**,
   923 chunks dropped behind it.

Working: threshold at the 80 ms floor, adaptive target above it, feeder stops when the target
is *reached* rather than when the next chunk would exceed it. **5 underruns in 18 s, nothing
dropped, `RUNNING`.**

⚠️ Two thresholds that must not be equal. If either is tuned, keep the gap.

### Confirmed against the reference host
All 18 status fields matched the .NET reading taken before the window, `/proc/asound` delay
read **501 ms** exactly as CARRYOVER.md section 3 records, and RX audio was real receiver audio
(crest factor 4.57 against 1.41 for a sine).

### PTT tail-wait, tested on the air at 5 W
149 ms queued on the device at unkey, `/api/ptt/off` reported `drained_ms: 149`, TX dropped
cleanly. Tested with a **10 s watchdog** as a backstop and power at **QRP**, not 200 W.

## 5. The tools, and why each fails closed

| tool | what it does | how it refuses to hurt the station |
|---|---|---|
| `tools/parity_check.py` | compares route **keys and types** against the reference host | allowlist **plus** a rule that any route whose final segment is not a read is refused. `on`/`toggle`/`tune` are not reads |
| `tools/walk_all_routes.py` | fires **state-changing** routes, including PTT | requires `/api/backend` to prove `simulated:true`. A 404 — which is what the reference host returns — is a refusal. **No `--force`** |
| `tools/coverage.py` | probes every reference route to measure what is implemented | same `/api/backend` guard |
| `--selftest` (host and client) | walks the startup path and exits | CI runs it under an external **timeout**: a hang is a failure |
| `--screenshot` (client) | renders the live window to a PNG | the only way to inspect a UI on a headless box |
| `--check-resolutions` (client) | walks the panel across seven screen sizes and measures every key | **refuses to run without a session** — the connect screen fits every resolution ever made and would pass while measuring nothing |

**A scope lock the operator must remember is not a lock.** Most of this API is state-changing
and many of those routes are GETs; a walker that simply fetched all 141 would key the
transmitter, change mode and retune the amp. So the guarantee is structural. Proven, not
assumed: the walker refuses the live host, and `/api/ptt/on`, `/api/mode/cw`, `/api/tune/amp`
and `/api/power/max` are all rejected by the parity guard.

⚠️ **The walker asserts against the RIG, not the route's own reply.** A route's response comes
from the handler under test, so it proves nothing. Every drive case reads state back out of
`/api/status` or `/api/status/full`.

---

## 6. Rules learned the hard way

These are the expensive lessons. Each cost real debugging, on this project or the last.

### Measurement
- **A test that cannot fail is not a test.** The obvious staleness check — `SIGSTOP` the
  process, re-query — freezes the HTTP server too, so the cache refreshes the instant the
  process resumes. It looks like a pass and measures nothing. Same shape as CARRYOVER.md §3's
  byte-count latency estimate, which read ~0 while 435 ms sat in the ALSA buffer: **an estimate
  whose failure mode is zero looks exactly like a working measurement.**
- **A measurement that counts only what you thought to look for is not coverage.** "131 of 141"
  counted only *exact* routes; 18 prefix families — the keypad, band select, every numeric
  setter — were not in the number at all.
- **Check the instrument before blaming the subject.** The first `/ws` probe reported a wrong
  first frame; the bug was in the probe, which discarded bytes the handshake `recv` had already
  pulled in.
- **Do not infer a hang from one early look.** Shutdown was reported as a deadlock; it was
  simply still shutting down. Measure the duration.
- **Look at the actual output.** A UI is not verified until someone has seen it render.

- ⚠️ **`assert()` IS NOT A TEST — CI DELETED 154 OF THEM.** CI builds
  `RelWithDebInfo`, which defines `NDEBUG`, which removes `assert(expr)` **expression and
  all**. `assert(q.Pop(out));` did not just stop checking: the `Pop` never ran, so the next
  line read `out[0]` on an empty vector and `audio_queue` **segfaulted on the runner while
  passing on the build VM** — which builds with no build type and therefore keeps asserts
  live. Six pushes of red CI, and the only reason it was noticed at all is that one assertion
  crashed instead of quietly vanishing. The tests use `CHECK()` from `tests/check.h` now: it
  always evaluates, in every build type, and names the file and line. Proven under
  `-O2 -DNDEBUG` with a deliberately failing check, not assumed.

### Truth in what the software says
- **If a capability is absent, its status route must say so.** The reference
  `/api/record/start` answers `{"status":"ok","recording":true}` while `Start()` sets
  `IsRecording = false`. A 200 means the route exists, not that anything happened.
- **Never fall back to a plausible value.** A failed CAT exchange reports *disconnected*, never
  the previous reading — that fallback is the 3.6-hour-stale-frequency bug. A named CAT device
  that cannot be opened **exits 1** rather than quietly using the simulator.
- **Report "arriving" and "playing" separately.** A stream that arrives but is inaudible is a
  device problem; one that does not arrive is a link or auth problem.
- **Do not invent a scale.** See §7.

### Protocol and porting
- **Read the driver; do not infer the verb.** Five CAT verbs guessed from the pattern of their
  neighbours were wrong: notch is `BC01`/`BC00` not `BP0`, width is `SH00<nn>` not `SH0<nn>`,
  RX antenna is the `EX030103` *menu item* not a CAT flag, RIT nudges carry a four-digit offset
  so a bare `RU;` is not a command, and monitor is `ML0000`/`ML0001` with a level restore.
  All five would have compiled, shipped, and failed only with the radio attached.
- **Match shapes exactly, and read them off the wire.** `/api/volume/get` returns a *percentage
  plus raw*; volume steps by **13**; `/api/volume/set/50` reads back **49** because
  percent→raw→percent is lossy in integer maths — and the reference does the same, so 49 is
  correct. A client with a slider should send percent and **trust the readback**.
- **Two locks that are not interchangeable**: `/api/lock/*` is the rig's CAT lock (`lock` in
  `/api/status/full`); `/api/vfo-lock/*` is a software lock (`vfo_locked` in `/api/status`).
- **`/api/tune` is the rig's internal ATU and is the wrong tuner for this station.** Each tuner
  names itself in its reply so a confirmation cannot just say "tuning".
- **Flush serial input before each command.** Otherwise a leftover reply from a timed-out
  command answers the *next* one, and every reply after that is off by one — each individually
  plausible. A frequency that is really the mode.

### Process
- **A signal handler must act, or not exist.** One that set a flag nothing read made the
  process unkillable and would have broken every `systemctl restart`.
- **Join your threads.** A pump thread with no destructor turned a clean "failed to bind,
  exit 1" into SIGABRT on every early-return path.
- **A string edit that does not match fails silently.** One dispatcher patch simply did not
  apply; prefix routing was absent entirely, and it nearly hid because `/api/mode/cw` is *also*
  an exact route. Edits now assert their anchor exists before writing.
- **Reject a bad config; do not fall back to defaults.** Starting on defaults runs the station
  on settings the operator did not choose and believes they changed — including the watchdog.
  And parse into a local, assigning only on success: writing as you go leaves a caller holding
  a half-applied config, which also made one test pass for the wrong reason.
- ⚠️ **Never `pkill -f` a pattern that also matches your own shell.** Cleaning up a throwaway
  simulated host with `pkill -f hamdeck-host` over ssh matched the **live service** as well —
  the ssh command line itself contained the string — and took the station host down for about
  40 s. The unit has `Restart=on-failure`, and a clean SIGTERM exit is not a failure, so
  nothing brought it back. Stop a service with `systemctl stop`, and a throwaway by the **PID
  it printed when it started**. (The radio was not keyed, and shutdown drops PTT and confirms
  by reading `TX;` back, so there was no open carrier — that is the safety net working, not a
  reason to do it again.)
- **Isolate test cases.** A shared object carried a rejected value into the next case, which
  then passed while reporting the wrong error.

---

## 7. Meter calibration — looked up, not invented

Numbers come from **Hamlib's Yaesu tables**, contributed by people with the actual radios.
Better than an assumption; still not a measurement of this station, and every reading says so.
The calibration lives in the **host** and is served by `/api/meters/scale`, so swapping the rig
moves every client's scale without shipping a new client. A client given no scale draws
**unlabelled** ticks rather than inventing one.

⚠️ **Three assumptions that would each have been wrong:**

| meter | the obvious guess | what the table says |
|---|---|---|
| S-meter | S9 at midpoint, raw 128 | **S9 is raw 160**; raw 128 is about **S7** — 1.5 S-units high, the difference between "5 by 9" and "5 by 7" in a report passed to another operator |
| ALC | full scale at raw 255 | **full scale is raw 64**; a `raw/255` bar reads **25%** when ALC is at 100% |
| Power | raw 255 = 100 W per the table | that table is for a **100 W** radio; this rig is **200 W**, so watts would read **half** |

Power is reported as **percent of rated output**, never watts. SWR is a ratio, turns red at
2:1, and comes from a hamlib *default tested on an FT-991* — curve shape right, breakpoints
unconfirmed, so one decimal not three.

⚠️ Hamlib defines the S-table for the FTDX-101**D**; its source says the code is shared with
the **MP**. Treated as applying to both. **If a reading looks wrong on the real radio, this is
the first assumption to check** — and the fix is to measure against a known source, not to
nudge the table until it looks nicer.

Sources: hamlib `rigs/yaesu/ftdx101.h` (`FTDX101D_STR_CAL`) and `rigs/yaesu/newcat.c`
(`yaesu_default_swr_cal`, `yaesu_default_alc_cal`, `yaesu_default_rfpower_meter_cal`).

---

## 7b. Client TX audio

Microphone → `/ws/tx` → the host, 48000/16/mono. Measured **93.8 KiB/s** out, which is exactly
48000 × 2 bytes, with the host counting 212 frames accepted and **zero dropped**.

### ⚠️ ARM and PTT are separate, deliberately
**Arm** opens the socket and takes the host's single-transmitter claim; **PTT** keys the rig.
Doing both on one press would put a WebSocket connect at the start of an over — the worst place
for a delay, and exactly where clipping is most noticeable — and would leave the claim in doubt
while the operator is already talking.

### Other choices worth keeping
- **Frames are sent only while the RIG reports keyed**, not while the button is pressed. If the
  rig is keyed by anything at all — another client, the mic button — this client transmits and
  mutes its receiver to match.
- **Capture keeps running while unkeyed and the audio is discarded.** Letting it back up means
  the first thing transmitted on the next over is several seconds of the room from *before* PTT
  was pressed. Stopping and restarting capture instead would put device start-up latency at the
  front of every over.
- **The rate comes from the host's config frame**, never assumed. A mismatch is not a subtle
  artefact — it is a chipmunk or a drawl going out on the air.
- **Mic resolved by NAME with fallback to the system default**, never "the first in the list",
  which on many machines is a monitor loopback. Transmitting the desktop's own audio output
  would be a memorable mistake.
- **The host's two refusal reasons are surfaced separately** — no transmit permission vs
  somebody else holds the transmitter. Collapsing them into "TX failed" sends people to the
  wrong fix.

### ⚠️ `--tx-test-tone` is a test facility and shouts about it
It transmits a synthetic tone so the path can be proven on a machine with no microphone. The
ARM button turns **transmit-red** rather than blue and every status line says **TEST TONE**,
because the one real risk of having it is somebody transmitting it thinking it is a microphone.

`--screenshot` now grows the window to the panel's natural height for capture only — normal
operation keeps the work-area clamp, which exists to stop exactly that on a real desktop.

## 7c. The PTT hotkey

Seven choices, not one, because any single key can collide with a keyboard driver, a game
overlay or a desktop environment on a given machine — the fix should be *pick another*, not
*give up*. HOLD or TOGGLE, both remembered.

### ⚠️ F13 is the best key and the worst default, at the same time
F13 is technically ideal *because no physical keyboard sends it*, so nothing can conflict with
it. That is also exactly why it cannot be the default: **if no keyboard sends it, the
operator's does not either.** It needs a footswitch, a macro key or a programmable keyboard
remapped to it — which is the setup most operators eventually want, and not one anybody has on
day one.

So the default is **Pause/Break**: on most full-size keyboards, pressable today, and almost
nothing else listens for it. F13/F14/F15 sit in the list for anyone with the hardware, with the
tradeoff written into the tooltip. Scroll Lock is there too — same idea, and on many keyboards
it lights an LED, which is a free transmit indicator.

### ⚠️ It is a WINDOW-FOCUS hotkey, and the UI says so
A *global* hotkey — one that works while another application is focused — cannot be done with
Qt alone. Windows `RegisterHotKey` delivers key-**down** only, so it can only ever be
press-to-toggle; hold-to-talk needs a `WH_KEYBOARD_LL` hook, which sees every keystroke on the
machine (a real privacy and antivirus-flagging consideration, not just an implementation
detail). X11 needs `XGrabKey` and fights desktop environments that already grabbed the
combination; macOS needs Accessibility permission granted by hand.

None of that is written. The status bar says **"PTT key: window focus only"** — calling it a
global hotkey when it is not would be the same class of lie as a status route reporting ok for
something it never did.

### Two safety properties, both tested
- **Auto-repeat is suppressed.** A held key repeats at the OS repeat rate; without filtering,
  the transmitter keys and unkeys several times a second for as long as the key is held.
  Tested: 1 press + 25 auto-repeats + 1 release produces **exactly 2 events**, not 52.
- **Losing focus while held unkeys.** A key held when the window loses focus never delivers its
  release, so the rig would stay keyed while the operator looks at another window — and only
  the host watchdog would stop it, minutes later. Tested in both HOLD and TOGGLE modes.

## 8. The client

Qt 6 + CMake. Section-6 traps designed in rather than discovered: audio devices by **name**
with fallback to the **system default** (never index 0), settings outside the install directory
with **no password ever stored**, **no default host** anywhere in the source, window geometry
**clamped to the work area** and re-centred, unknown flags abort.

Things it does that a naive client would not:
- the **PTT button reflects the rig's `tx`**, never its own checked state;
- **DSP toggles reflect the rig**, so another client or the radio's front panel is followed;
- **RX mutes from the rig's `tx`** and drops the queue on unmute, so the operator returns to
  live audio rather than a replay of themselves (delayed auditory feedback, CARRYOVER.md §4c);
- it **counts down the host's watchdog** via `tx_timeout_in`;
- it **shows `stale`** instead of hiding it — the readout greys out.

⚠️ **The panel lives in a `QScrollArea`, and that is load-bearing.** Clamping the window
geometry is not enough on its own: Qt honours the layout's minimum size hint, so a panel taller
than the work area forces the window bigger regardless of what `setGeometry` asked for — and
then the title bar is off-screen and the app cannot be reached at all. The selftest caught this
the moment the DSP row and keypad were added, which is exactly what it is for.

⚠️ **`QWebSocket` does not share the REST cookie jar.** The audio stream was refused at the
upgrade and the panel silently had no receiver. The token goes as `?token=`, which is exactly
why the host accepts that transport.

---

## 8b. The QML front end — method, and what it cost

**QML (Qt Quick), per CLAUDE.md**, replacing the Qt Widgets panel. Both build from the same
CMake; the Widgets one is kept only until this is proven and is superseded.

**The port was cheap because the C++ core was already separate.** `api_client`, `rx_audio`,
`tx_audio`, `ptt_hotkey`, `settings` and the host-supplied meter scale are reused unchanged —
only the view was rewritten, behind a `Backend` facade of `Q_PROPERTY`/`Q_INVOKABLE`. Every
rule that matters (PTT reflects the RIG, RX mutes from the rig's tx, the host's watchdog is
counted down, an uncalibrated meter stays unlabelled) lives in that shared core, so it could
not be lost in the port. **A UI that reimplements those rules is a UI that can disagree with
them.**

### Brand, applied verbatim
Tokens copied from `~/hamdeck-site/brand/BRAND.md` into `Theme.qml` and mirrored in
`src/theme.h`. ⚠️ The previous palette was **invented**, including an amber two digits off the
one the doc defines — which the doc calls out as a bug by name.

⚠️ **An audit found seven more invented colours** in the QML (a lighter red, a mid-green, tick
greys). All replaced with tokens. The audit is one line and worth keeping:

```sh
grep -ohE '"#[0-9A-Fa-f]{6}"' client/qml/HamDeck/*.qml client/qml/*.qml | sort -u
```
Anything outside `Theme.qml`'s twelve is an invention.

### Fonts are bundled, not assumed
Barlow Condensed (display), IBM Plex Sans (body), IBM Plex Mono (data) ship in the binary
with their OFL licence. BRAND.md makes the point about a logo that names a font and falls back
to whatever the viewer has; a panel rendered in a substitute face is not the panel designed.
The selftest asserts both families are actually present.

### Three bugs worth remembering
- ⚠️ **Heap corruption on exit**, traced by backtrace to `~ApiClient` during Qt Network's
  thread teardown. The Widgets build tore down explicitly in `closeEvent` and never saw it;
  QML had no equivalent and relied on destructor ordering across Network, WebSockets and
  Multimedia. Now `Backend::shutdown()` stops producing, then the transports, then the
  session — wired to `aboutToQuit` so it runs however the app ends.
- **A `Repeater` inside a `RowLayout` collapsed to zero width** and packed three columns
  against the left edge. Explicit `ColumnLayout`s with `fillWidth` did the same. Positioning
  by fraction of width fixed it. A three-item readout does not need a layout negotiation.
- The QML module needs its own directory matching the module name, and **every** component
  declared in `qmldir` — not just the singleton.

## 8c. Administration

`/api/admin/*` for user management: list, add, change password, remove, grant or revoke
transmit, list sessions, kick, and lockdown. Adding a user no longer means editing a file by
hand and restarting.

### ⚠️ A third gate, and it runs on the LOCAL listener too
Admin routes need **admin**, not merely a session — they add users, change passwords, revoke
transmit rights and end other people's sessions. The gate sits beside the auth and VFO-lock
gates, before routing, so an admin route added later is covered without anyone remembering.

It is enforced on the loopback listener as well. Local callers skip *authentication* because
the kernel vouches for where they came from, but **"is this an admin" is a question about a
user, and there is no user on an unauthenticated port.**

Verified: no session → 401, non-admin session → **403**, admin → 200.

### ⚠️ Three side effects that are easy to miss
- **Revoking transmit must reach LIVE sessions.** A session carries its own copy of the flag,
  so updating only the user record means the revocation does nothing until that operator logs
  out — precisely when it no longer matters.
- **Changing a password invalidates existing sessions.** A change that leaves old sessions
  working has not revoked anything, which is usually the entire reason for the change.
- **Removing a user takes their sessions with them.** Otherwise the account is deleted
  everywhere except where it counts.

### ⚠️ Refusing to remove the last admin
The reference host does not check this. Removing the only admin leaves a host nobody can
administer — recoverable only by hand-editing a config file and restarting, on a box that may
be at the far end of a radio link. The route answers **409** and says why.

### ⚠️ The config writer PRESERVES KEYS IT DOES NOT KNOW
A writer that serialises its own struct silently deletes everything else in the file — a
setting a newer build added, a note the operator left. It reads the existing document, updates
only what it manages, and writes that back **via a temp file and a rename**, so an interrupted
write cannot leave a half-written config that then refuses to parse on the next start.

Proven by putting two unknown keys in the live config, adding a user, and checking both
survived. Also proven: the user survived a restart, and every stored password is a PBKDF2
hash.

### The failure mode was honest before it was fixed
The first attempt reported `user added but NOT saved: cannot write .../config.json.tmp` — the
service user owned the config file but not its **directory**. It said so rather than reporting
success and losing the change at the next restart.

### Session listings show a token PREFIX only
A full session token in an admin listing is a credential in a log, a screenshot and a support
ticket.

## 8d. Resolution awareness

⚠️ **THE PANEL WILL RUN ON SCREENS NOBODY HERE OWNS.** The same window is a third of a 4K
monitor and taller than a 1024x600 shack netbook. Left alone, Qt draws every one of those at
the same pixel sizes: unreadable across the room on the first, clipped on the last.

### Two problems, two mechanisms — conflating them is the bug
| | what it answers | where it lives |
|---|---|---|
| **Density** | how big a thing is drawn | one number, `Backend::uiScale`, applied through `Theme.u()` / `Theme.f()` |
| **Reflow** | how many fit on a row | `Theme.cols()`, evaluated against the width actually available |

A panel that does only density is clipped on a narrow window; one that does only reflow is
unreadable on a 4K. Every size in the QML now goes through `u()` or `f()` — the audit is one
line, and anything it prints is an unscaled constant:

```sh
grep -nE 'pixelSize: [0-9]|spacing: [0-9]|Height: [0-9]' client/qml/*.qml client/qml/HamDeck/*.qml
```

- **Type has a floor (`f()` never goes below 9 px), sizes do not.** An unreadable legend is
  worse than a cramped layout: the operator cannot tell what the key does.
- **`Theme.scale` never multiplies devicePixelRatio.** Qt has already divided it out; folding
  it back in draws a HiDPI panel at twice the size asked for. `dpr` is carried only so the
  status line can *say* what it found.
- **Auto = fit to the smaller axis**, `min(availW/1280, availH/900)`, clamped 0.80–1.75.
  Fitting width alone would scale a 2560x1080 ultrawide up until the panel no longer fits
  vertically — and this panel is far taller than it is wide.
- **The mode is stored as a LABEL** ("Auto", "125%"), not an index, so inserting a mode later
  cannot silently move every operator to a different scale.
- **The screen is re-reported when the window is dragged to another monitor.** A scale computed
  once at startup is wrong the moment the panel moves from the laptop to the desk monitor,
  which is a daily event, not an exotic one.
- **PTT and ARM keep their size at every resolution.** They scale; they never wrap into
  something small. The one key that must be hit without looking is not where space is saved.
- **The status bar drops items right-to-left as the window narrows**, in reverse order of how
  much they matter. Elided text in a status bar is worse than absent text — "audio: strea…"
  reads as a fault. The transmit state, on the left, never drops.

### ⚠️ ScrollView resized the panel behind our back
The panel was in a `ScrollView`, which **sizes its content item to the content's natural width
and ignores a width binding on it**. The panel came out **691 px wide inside both a 1024 px and
a 448 px window**, so every row reflowed against a width the keys were never given and the
right-hand column fell off the edge. It is a `Flickable` with an explicit `ScrollBar` now —
a Flickable leaves its children's geometry alone.

That is also why reflow measures `panelCol.width` rather than the window's: a scrollbar, a
margin and a container that resizes things sit between the two numbers.

### The walk, and the three ways it lied before it worked
`--check-resolutions [dir]` sets the screen to each of seven sizes, sizes the window twice per
screen (work area, and the app's own minimum), and for every visible key measures: **squeezed**
(drawn narrower than its own minimum, i.e. the legend is clipping), **off-edge** (past the right
edge of the window), and the **smallest key in pixels**. It writes a PNG per size, because a
number saying "fits" and a picture showing a readable panel are different claims.

⚠️ **Every one of these passed cleanly while measuring nothing:**

1. **Measuring `panelColumn.implicitWidth`** — the groups anchor their contents, which stops
   implicit width propagating, so it read the margins: **20 px at every resolution, seven
   passes.** The section-6 trap (*an estimate whose failure mode is a small number looks exactly
   like a working measurement*) inside a test written to avoid it.
2. **Finding keys by QML type name** — the engine names generated metaobjects its own way, so
   it matched **zero keys** and reported a clean sheet. Keys carry `objectName: "panelKey"` now,
   and **no keys found is a failure**, not a pass.
3. **`findChildren()` instead of the visual tree** — QObject parentage misses everything a
   `Repeater` created, which is the band row, the mode row, the receiver row and the keypad:
   **17 keys of 58.** A measurement that counts only what you thought to look for is not
   coverage.

⚠️ And one that made the *panel* look broken when the instrument was: **the resize is not
finished when `setWidth()` returns.** It arrives as a posted event and `grabWindow()` renders
without draining the queue, so the walk measured a 459 px panel inside a 1920 px window and
blamed the layout. It pumps the event loop now, and prints the width **the window reports**
rather than the width it was asked for.

### Verified 08/30/2026
58 keys found at every size; **0 squeezed, 0 off-edge** across 1024x600, 1280x720, 1366x768,
1600x900, 1920x1080, 2560x1440 and 3840x2160, at both the full work area and the app's minimum
width. Smallest key **46 px** (0.80x) to **102 px** (1.75x). Screenshots inspected by eye at
1024x600, 1920x1080, 3840x2160 and a deliberately narrow 620x1000 — the keypad, tuner row and
transmit row wrap as intended and nothing is clipped.

Run it against a **throwaway simulated host**, never the station: it needs a session, and the
panel does not care whether the rig is real.

```sh
HAMDECK_CONFIG=/tmp/sim/config.json ./hamdeck-host &      # own ports, own record path
QT_QPA_PLATFORM=offscreen ./hamdeck-qml --host 127.0.0.1 --port <sim> \
    --user <u> --password <p> --check-resolutions /tmp/sim/shots
```

`--ui-scale <f>` forces a scale (and the Display group **says** the scale came from the command
line), and `--screenshot-size WxH` captures at a size this box has no monitor for.

### The dropdowns are ours now
⚠️ Qt Quick Controls' Basic style draws a **light** control — white field, white popup, black
text — which put three white boxes on a near-black panel. `PanelCombo.qml` styles field, popup,
delegate and indicator from the brand tokens, so the four places that use one cannot drift.
Its popup is capped at 60% of the screen height: a machine with a dozen audio devices would
otherwise open a list running off the display, and the device the operator wants is the one
they cannot reach.

⚠️ A new component must be declared in **both** `qmldir` **and** `resources.qrc` — miss either
and it fails at runtime only.

---

## 9. Adding radios nobody here owns

The simulator makes this tractable: look up the CAT set, write a profile, run the walker
against it. Two things to plan for.

- The verbs are currently **hardcoded for the FTDX-101**. A second radio wants a **rig-driver
  abstraction** — a per-model verb table that both the real transport and the simulator read
  from. Worth doing before the second radio, not the third.
- ⚠️ **A simulator written from the manual tests your reading of the manual, not the radio.**
  It catches parser bugs, field offsets and wrong widths — most of them — but not a manual that
  is wrong or a rig that deviates. A new model ships as *believed working, untested on
  hardware* until someone with that radio confirms. Cross-check verbs against hamlib, which is
  validated against real rigs rather than PDFs.

---

## 10. Next session: start here

1. Read §1 and §2. Check the push blocker is still standing.
2. `HAMDECK_BUILD_HOST=<target> ./sync.sh`, then `ctest` — 8 tests should pass.
3. Start the host, run `tools/coverage.py`, `tools/walk_all_routes.py` and
   `tools/parity_check.py` against it. All three should be clean.
4. Then pick up: **client TX audio + F13 hotkey** (no radio needed), or **book a hardware
   window** for ALSA, the PTT tail-wait and `/api/ptt/off`.
