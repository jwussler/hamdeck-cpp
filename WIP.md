# HamDeck C++ — work in progress

Mid-build handover. Written 08/30/2026. Read §1 and §2 before touching anything.

> **THIS REPO IS PUBLIC.** Nothing station-specific goes in it — no hostnames, addresses, VM
> ids, tunnel details, or live readings that say what the station was doing. That applies to
> **commit messages** too. Site detail lives in the gitignored `SITE.md`.
> CARRYOVER.md §6 states the narrow version: *a hostname in a public repo points every install
> at that station.* It generalises.

---

## 1. Status at a glance

> **The open work is in §10 and in `AUDIT-CSHARP.md`** — the reference app walked bit by bit,
> with what is done, what is left, and what is deliberately not being done.
>
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
| `tools/hotkey_check.sh` | drives the PTT key with `xdotool` under a real WM and asserts the RIG keyed | **refuses any host not reporting `simulated:true`** — it keys a transmitter |
| `tools/set_password.py` | sets a user's password or adds a user without the admin API | prompts rather than taking a password in argv; preserves unknown config keys; temp-file-and-rename |
| `tools/placement_check.sh` | opens the window under a **real WM** (openbox/Xvfb) and reads the decorated frame back with `xwininfo -frame` | it is a **release gate** in CI: no installer is built if the frame lands off-screen |
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
- ⚠️ **"login failed (0)" was a lie, and it cost a live debugging session.** The login handler
  read only the HTTP status attribute; on a DNS failure, a refused connection or a timeout
  there is no status at all and it reads **0**, so a host that was never reached reported a
  **rejected password**. The operator re-typed credentials at a box that was not answering.
  A transport failure now says `no answer from <host:port> - <reason>` and the
  "login failed:" prefix is gone, because it contradicted the message it was glued to.
  Proven both ways: an unresolvable name, a shut port, and a real 401 that still reads
  `Invalid credentials`. Same rule as the audio status line — **"not arriving" and "refused"
  are different problems and must not share a message.**
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

## 8c-2. Where the window lands — and what shipping 0.1.2 cost

⚠️ **0.1.2 shipped a window nobody could move.** A fresh install placed itself at **(0,0)**.
`x`/`y` position the **client area**, not the frame, so the title bar sat *above* the top of
the screen — no drag handle, and the close button was on it. The function that did this already
carried a comment about title bars ending up out of reach.

**The failure was not the arithmetic, it was shipping an installer without ever watching the
window open on a desktop.** Everything up to that point was verified headless, where there is
no window manager and no decoration, so the bug was invisible by construction.

### Fixed, in two parts
- **`client/src/place.h/.cpp` — `PlaceWindow()` is pure arithmetic**: saved geometry, work area,
  scale in; a rectangle out. No window, no screen lookup, so the cases a headless box cannot
  produce can be tested directly.
- **The work area has an ORIGIN, not just a size.** `Screen.desktopAvailableWidth/Height` are
  sizes only, so a taskbar along the **top** or the **left** — and a second monitor, which
  starts at a virtual-desktop offset — would be placed as if the work area began at 0,0. The
  origin now comes from `QScreen::availableGeometry()` in C++; QML's sizes are only a fallback.
- **A first run is centred**, never cornered. **A restored `y` is floored** below the work
  area's top whatever was saved, so an older build's `0` cannot reproduce the trap on upgrade.
  A position from a monitor that is gone is **re-centred**, not clamped.
- **`--reset-window`** clears a stored position and exits without opening a window — the rescue
  path for somebody whose panel is already unreachable, because the alternative is asking them
  to find an INI file.

### Two instruments, because one was not enough
| | what it covers |
|---|---|
| `ctest -R place` (9 cases) | the arrangements this box cannot make: top/left taskbar, second monitor at an offset, unplugged monitor, oversized saved size, tiny screen |
| `/tmp/placement.sh` (Xvfb + **openbox**) | where the **decorated frame** actually lands, read back with `xwininfo -frame` |

⚠️ The unit test includes **"a good saved position is left alone"**. Without it, "always on
screen" is satisfiable by ignoring the operator's position every launch, and the suite would
pass while the app threw away where they put it.

⚠️ The WM script's first version wrote the fake settings to `HamDeckClient.**conf**`; Qt's
`IniFormat` writes `HamDeckClient.**ini**`. The file was ignored, so the "restored from a saved
0,0" case silently ran the *fresh* path and reported a pass. Same shape as everything in §8d:
**it was caught by reading the number, not by the test failing.**

### ⚠️ The gate passed 6/6 while measuring almost nothing
The first version of `placement_check.sh` went green in CI and was wrong twice over. Both are
worth keeping, because both read exactly like a working measurement:

1. **`xwininfo -id <client> -frame` returns the CLIENT rectangle.** The flag reads like it asks
   for the frame; it does not. So the script that existed to check the decoration never looked
   at a decoration — it re-checked the client rect, which the unit test already covers. It now
   reads **`_NET_FRAME_EXTENTS`**, the window manager's own statement of how far the decoration
   extends on each side, and **fails if that property is absent** rather than reporting ok on a
   number it never took.
2. **`QSettings` honours `XDG_CONFIG_HOME` over `HOME`.** Setting `HOME` alone works on a box
   where `XDG_CONFIG_HOME` is unset and does nothing on a GitHub runner, where it is set — so
   in CI the two *saved-geometry* cases silently ran the **fresh** path and passed. The script
   sets both now, and each saved case **asserts the saved size came back** (900 wide; no default
   produces that), so a settings file the app never read is a failure instead of a green tick.

⚠️ And with the decoration finally being measured, it found a **second bug in the fix**: a
window whose client area starts at `x=0` has its **left border at −1**. Only the top had been
reserved. `PlaceWindow` reserves a border on the left, right and bottom as well — on Windows
the invisible resize border is about 8 px, so the edge you grab to resize was off the screen
even with the title bar visible.

⚠️ One assertion was itself too strict: a saved 900x800 **legitimately** clamps to 900x560 on a
1024x600 screen, and asserting the height came back at 800 failed the tool for behaving
correctly. Width is the tell; height is a bound.

### Measured 08/30/2026, after the fix
**Frame including decoration** (`_NET_FRAME_EXTENTS`, openbox, 20 px title bar) fully on screen
at 1024x600, 1366x768, 1920x1080 and 2560x1440 on a first run — centred, e.g. client `432,84`
with the frame at `431,64`. A saved `0,0` restored to client `10,48`, frame `9,28`. A saved
`3200,400` from a vanished monitor re-centred. A saved 900x800 on a 1024x600 screen clamped to
900x560 and still fully framed. **12/12 placement cases, 3/3 client tests**, and the resolution
walk still clean.

⚠️ **Windows is still unwatched.** No one here has seen the window open on Windows — the
placement logic is defensive and the maths is tested, but that is not the same claim.

---

## 8c-3. The hotkey was dead, and its test passed

⚠️ **`tests_hotkey.cpp` proves the state machine — hold, toggle, auto-repeat suppression, unkey
on focus loss — and every case passes. None of it ever ran in the application.** QML's
`Keys.onPressed` fires only on the item that holds **focus**, and the panel is full of things
that take it: the connect screen's password field on startup, then every dropdown, slider and
the scroll area. Two green lights and a transmitter that never keyed.

**Keys are filtered at the APPLICATION now** (`Backend::eventFilter`, installed on
`QGuiApplication`), which sees the key whatever holds focus and consumes **only** the configured
PTT key — so typing a frequency or a password is untouched. The QML handler is gone rather than
kept alongside: two paths would double-fire and a toggle would cancel itself.

### `tools/hotkey_check.sh` — the test that could have caught it
Runs the real binary under openbox on Xvfb, drives the key with `xdotool`, and asserts by
reading **the rig's `tx` state out of `/api/status`** — never the client's own belief, the same
rule the route walker follows.

⚠️ It **refuses any host that does not report `simulated:true`**. Keying a real transmitter to
test a keyboard shortcut is not acceptable, and the guard is structural rather than something
the operator has to remember.

It includes the case that matters in real use: **press the hotkey after clicking something in
the panel**. That is the state a working unit test cannot reach and the one an operator is
always in.

Measured after the fix: HOLD keys on key-down and unkeys on key-up; TOGGLE keys on the first
press and unkeys on the second; both still work after clicking in the panel. Before the fix,
every one of those read `tx=false`.

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

## 8e. The TGXL tuned nothing, because it never keyed the radio

⚠️ **The first port talked to the tuner and never touched the radio.** Its own header called
itself "safe to call from a request thread: it touches the network, not the serial port" —
which was true and was the bug. The tuner has nothing to measure without a carrier, so the
button appeared to do nothing. Found by walking `Services/Tuners.cs` in the reference host,
where the sequence is the radio's, not the tuner's:

```
1 save power and mode   2 set 15 W, CW   3 connect   4 KEY   5 autotune
6 poll tuning=          7 UNKEY, restore power and mode
```

⚠️ **Steps 3 and 4 are swapped relative to the reference, deliberately.** The C# host keys
*then* connects, so a tuner that is switched off gets **15 W into the antenna for the whole 3 s
connect timeout**, tuning nothing. The tuner only needs the carrier from step 5, so connecting
first costs nothing and an unreachable tuner now produces **no RF at all** — measured: `tx`
never goes true.

### Three more divergences the walk found in the same file
- **Completion was "tuning is 0", not "tuning went 1 then 0".** The old condition was
  `seen_tuning || elapsed > 2 s`, which reports a **completed tune at 2.001 s against a tuner
  that never started** — the exact failure its own comment claimed to prevent. It now needs
  `tuning=1` *and* 2 s, disarms on the connect burst's early 1→0, and gives up at 5 s if
  tuning never starts.
- **A second press is a STOP.** The reference button is a toggle; this returned
  "already-tuning", leaving an operator watching an unexpected carrier with nothing to press.
- **`Tune()` blocked the request for up to 45 s.** The reference returns immediately and
  reports progress through `tgxl_tuning`. Blocking froze the panel for the whole carrier.

⚠️ **`/api/status` and `/api/health` had `amp_tuning` and `tgxl_tuning` HARDCODED to `false`.**
A tune in progress was invisible to every client — they could not show the carrier or offer the
press that stops it. Reported live now.

### Verified on the simulator, with a fake tuner — `tools/fake_tgxl.py`
It reproduces the **connect burst** (0, 1, 0 within milliseconds) that the completion logic has
to survive, and `--never-start` covers the tuner that answers but never tunes.

| case | result |
|---|---|
| normal tune | `tx=true, power=15, mode=CW, tgxl_tuning=true` for the tune, then unkeyed and **restored to 5 W USB** |
| second press | `action:"stopped"`, unkeyed and restored within 2 s |
| tuner never starts | gives up at 5 s, unkeyed and restored — not a 45 s carrier |
| tuner unreachable | **`tx` never goes true**, power and mode restored |

⚠️ **`AmpTuner` is still a stub in this host** while the reference has the full sequence: 20 W
CW, a **10 second** carrier, then 100 W and the original mode back. `/api/tune/amp` answers
"not configured". Local-only, and not yet ported.

---

## 8f. The transmitter keyed into silence, because the client refused the microphone

⚠️ **The panel armed, said "armed", and sent NOTHING.** A live keyup produced `tx=true`, ALC
6%, **PO 0%** and a playback substream still reading `state: PREPARED, hw_ptr: 0` — not one
frame had ever been written to the codec. `/api/backend` settled it in one read:

    tx_accepted: 0   tx_dropped: 0   tx_queue: 0

Not accepted-and-dropped. **Zero arrivals, ever.** That exonerates the host, the ALSA sink and
the rig in a single measurement, and puts the whole fault between the microphone and
`sendBinaryMessage`.

### The bug: OpenMic() demanded one format and gave up
It asked for exactly 48000 Hz / 16-bit / **mono** and returned false on anything else. A USB
microphone that enumerates as **stereo** — which is how many of them appear in Windows shared
mode — failed on the channel count alone. 48000/16/mono is the **host's wire format**; it is not
a requirement anyone can place on the operator's microphone.

**It now negotiates**: exact format first (unchanged fast path), then the device's native rate
and channel count, then its preferred format including Float. `PcmConverter` downmixes to mono
and resamples to 48 k in the client, so the wire format never changes.

⚠️ **Average the channels, never take the left one.** A stereo-enumerating mic commonly has the
capsule on one channel and silence on the other; picking a channel is a coin flip between full
audio and a dead transmitter.

### Three things this failure got away with, and what now stops them
- **The status line said `no microphone` and nothing else** — no reason, and it ran off the
  bottom of the window. It now carries the reason, and names the negotiated format
  (`tx: transmitting · mic 44.1k/2ch→48k mono`) so a converted path is visible rather than
  inferred.
- **The error named what we ASKED FOR, never what was on OFFER.** "cannot capture 48000 Hz/
  16-bit/mono" sent an evening's debugging to the wrong end of the chain. It now prints the
  device's actual supported ranges.
- **Nobody could see a device's capabilities without a rebuild.** `--list-audio` prints every
  capture device, its rate and channel ranges, its preferred format, and the direct answer to
  the one question `OpenMic` asks. Before the QML loads, so it works when the panel is the
  thing misbehaving.

### Tests — `client/tests_pcm.cpp`, 17 cases
⚠️ **Chunk continuity is tested by MEASURING THE SEAM, not by counting samples.** A resampler
that reset its phase every chunk still produces about the right number of samples — the count
is exactly the thing that looks fine while the audio buzzes at the chunk rate. The test
resamples one second in 441-sample chunks and compares the largest sample-to-sample step
against the continuous version: **346 both ways.**

Writing it also found a real off-by-one: the first chunk had no predecessor, and inventing one
by repeating the first sample made one second of 44.1 k resample to **48001** samples.

### The host's transmit path had NEVER RUN — now measured, `tools/tx_path_test.sh`
⚠️ Because the client never sent a frame, `accept → queue → pump → codec` had **never executed
once** on real hardware. It was assumed working; it was untested code that compiled.

It is now proven, with `snd-aloop` standing in for the codec so what the host writes can be
recorded and measured rather than counted:

| | |
|---|---|
| audio recovered | 3.10 s for 3.00 s sent |
| peak | **8000** — exactly what was sent; no clipping, no attenuation |
| rms | 5565 against 5657 for a pure sine (98.4%) |
| 700 Hz vs neighbours | **326:1** |

⚠️ **Counting arrivals is not proof.** A sink writing silence, or writing at the wrong rate,
gives a perfectly healthy `tx_accepted`. The script records the audio and measures it, and it
FAILS on a silent recording, a wrong peak, or a tone that is not cleanly dominant.

The script is simulated-rig-only and refuses to run against a real radio; `ws_tx_send.py`
refuses unless `/api/backend` says `simulated:true`.

### ⚠️ Two findings from building that rig
- **`keyed` is NOT an accept gate.** `TxAudioReceiver::Accept` takes the rig's tx state but uses
  it only to decide whether the queue may be **trimmed** when full — audio is queued and pumped
  to the codec whatever the rig is doing. The client is the only thing that stops audio flowing
  while unkeyed. Harmless with VOX off; **with VOX on, a client that sent while unkeyed would
  key the transmitter.** Not changed — it is a deliberate-looking design and the call is Joe's.
- **Stray test hosts on the station box**, one up 4h23m, squatting on 5011. Simulated with null
  sinks so the radio was never at risk, but they are debris. Cleared. `tx_path_test.sh` traps
  its own exit so it cannot add more.

### ⚠️ IT WORKED, AND THE PANEL SAID OTHERWISE — the status line was stale
After 0.1.8 the station showed a red **`armed, NO MICROPHONE:`** with **nothing after the
colon** — and an empty reason is impossible for a real failure, because all three `OpenMic`
paths set one. That was the tell.

`ArmedChanged` was emitted **only when OpenMic FAILED**. The panel renders its line when the
SOCKET connects, which is the moment *before* the microphone is opened; on success nothing was
emitted, so the pre-mic snapshot stayed on screen for the life of the session. A working
microphone looked broken indefinitely.

Every path out of that branch now signals. ⚠️ **A UI that reports only failures cannot show a
recovery** — the success case needs a signal exactly as much as the failure case does.

### PROVEN ON THE AIR-SIDE HARDWARE — 08/31/2026 02:52
First audio ever to reach the station host from the client:

| | |
|---|---|
| tx_accepted | **0 → 10 → 182 in two seconds**, then 214 |
| tx_dropped | **0** |
| device_queued_ms | 68 → 51, steady |

The microphone, the negotiation, the converter and the socket all work. ⚠️ **The rig was NOT
keyed for that test** (`"tx":false` throughout), so PO stayed 0 — the host accepts audio
regardless of key state, so arrival proves the audio chain and says nothing about RF.

⚠️ **Still unproven: Windows microphone capture.** Qt's Linux audio enumeration is hard-wired to
PulseAudio in this build, and installing an audio server on the station box is not acceptable —
PulseAudio grabs ALSA cards and that box owns the codec. So the client's capture side cannot be
exercised here. Which of the three `OpenMic` failures the fifine hits is answered by
`--list-audio` and by the panel's own mic line.

---

## 8g. ON THE AIR — 08/31/2026 03:23, and the three bugs between here and there

First voice through the C++ host:

| time | tx | tx_peak | ALC | PWR |
|---|---|---|---|---|
| 03:23:51 | true | 15245 | 6% | 0% |
| 03:23:52 | true | 25576 | **100%** | 5% |
| 03:23:53 | true | 26218 | **100%** | **33%** |
| 03:23:54 | true | 26819 | 98% | **69%** |

### What actually stood in the way
1. **`OpenMic()` refused the microphone** instead of negotiating (§8f).
2. **A muted microphone, invisible to every counter we had.** Frames arrived, the queue
   behaved, `hw_ptr` advanced at 48 kHz, zero drops — all of it exactly as it reads when the
   audio is real. ⚠️ **`tx_peak` exists because of this.** Counting frames never proves there
   is SOUND in them.
3. **`/api/remote-tx/status` invented all three fields**, reporting the rig's TX flag as
   `mod_source_rear` behind a comment claiming it read through. It said the radio was on MIC
   when it was already on REAR/USB — so a correct write was reported as a failed one, and the
   hunt went to the wrong end of the chain. ⚠️ **A confident wrong answer is worse than no
   answer: it gets believed.**

⚠️ **The C# walk could not have caught #3.** `AUDIT-CSHARP.md` compared route INVENTORIES —
the route existed, so it was ticked — and the fabricating status route agreed with itself.
**Comparing route lists is not comparing behaviour.** The test that would have caught it is:
call the route, then read the radio back through something that is not the route under test.

Also fixed: `/api/remote-tx/on` fired three menu writes back to back where the reference host
spaces them **50 ms** apart.

### ⚠️ EVERY SLIDER IN THE PANEL WAS DEAD
`Knob.qml` had `value: pressed ? value : knob.value` — a binding referring to the Slider's OWN
value. The instant `pressed` went true QML saw a binding loop, broke the binding, and the
control froze. Volume, mic gain, RF gain, RF power, CW speed: none of them draggable, and the
operator could not turn down a microphone pinning ALC at 100%.

⚠️ **`qml_selftest` and `--check-resolutions` both passed the whole time**, because the loop
only forms on the FIRST PRESS. A control no test ever TOUCHES is a control nobody has tested —
the same lesson the global PTT hotkey taught, learned again.

### Open: ALC is pegged
Audio arrives at peak ~26,800 of 32,767 (82% FS) and ALC sits at 98-100%. Mic gain is 100% and
RPORT GAIN is 50. Wants trimming to ALC peaking 50-70%, not pinned — pegged ALC on SSB is
splatter. Not yet done; the sliders had to work first.

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

⚠️ **The open work is a LIST now, not a memory.** `AUDIT-CSHARP.md` is the whole surface of the
reference app walked bit by bit; everything below is what is still open from it. Gaps were
being found one at a time, in use, by the operator — the hotkey that never keyed, the tuner
that never transmitted, direct entry that was never ported. Each cost a rebuild and a reinstall.

### Getting going
1. Read §1 and §2.
2. `HAMDECK_BUILD_HOST=deck ./sync.sh`, then `ctest` in `build/` — **11 tests**.
   Client: `cmake -S client -B client/build -G Ninja`, then `ctest` — **4 tests**
   (hotkey, place, freq, qml_selftest).
3. Deploy with `tools/deploy.sh`, **never** `sync.sh` — it installs nothing and the service
   will keep running an older binary while you "verify" against it.

### Open, in the order they are worth doing

**Host services the reference has and this does not:**

| what | why it matters | notes |
|---|---|---|
| **TCP CAT proxy** (`Services/TcpCatProxy.cs`, 143 lines) | listens on **localhost:4532** and forwards CAT from N1MM through the same lock as the poller — **this is what removes the need for a virtual serial-port splitter** | probably the highest-value item left. N1MM: Configure Ports → TCP → 127.0.0.1:4532 |
| **WaveLog server** (`Services/WaveLogServer.cs`, 289 lines) | WaveLogGate-compatible: HTTP **54321** for QSY from the bandmap, WS **54322** status, and posting to the Wavelog API | wanted if the log is in use |
| **CW keyer** (`Services/Keyers.cs`, 127 lines) | sends CW text, five CW memories | `/api/cw/*` currently answers `available:false`, honestly. ⚠️ Check every verb against the manual **and** hamlib — five CAT verbs guessed from their neighbours were wrong last time (§6) |

**Panel controls still missing** (routes all exist on the host):

| control | route |
|---|---|
| Memory recall | `/api/memory/recall/{m}` |
| Presets | `/api/preset/`, `/api/admin/presets` |
| Voice memories | `/api/voice/play/{n}`, `/stop`, `/status` |
| SSB out level | `/api/ssb-out-level/get|set` |
| Remote TX | `/api/remote-tx/on|off|gain|status` |
| RX antenna | `/api/rxant/1`, `/api/ant/rx/toggle` |
| Rig internal ATU | `/api/tune` — ⚠️ the **wrong** tuner for this station; label it plainly if it is added at all |

**Deliberately not doing:**
- **DX cluster** (`/api/cluster/spots`) — 404s on the reference host too. Matching that is
  correct, not a gap.
- **FlexKnob**, **Kmtronic relay** — hardware this host does not have.
- **Hold-to-talk on the GLOBAL hotkey** — needs a `WH_KEYBOARD_LL` hook that sees every
  keystroke on the machine. The reference refuses it and so do we; the window-focus key still
  offers hold.

### Open questions, not code
1. **The Windows installer is unsigned**, and the global hotkey has only ever been exercised by
   the operator, not here — there is no Windows box in this loop.
2. **`hamdeck-site` still has no remote.** Backed up and restore-tested, but a bundle on a NAS
   is not a remote.
3. **The `joe` test credential is deliberately stable** while testing — Joe rotates it himself
   when done. Do not change it unasked (`tools/set_password.py`).
