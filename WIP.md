# HamDeck C++ — work in progress

Mid-build handover. Written 08/30/2026. Read §1 and §2 before touching anything.

> **THIS REPO IS PUBLIC.** Nothing station-specific goes in it — no hostnames, addresses, VM
> ids, tunnel details, or live readings that say what the station was doing. That applies to
> **commit messages** too. Site detail lives in the gitignored `SITE.md`.
> CARRYOVER.md §6 states the narrow version: *a hostname in a public repo points every install
> at that station.* It generalises.

---

## 1. Status at a glance

| | |
|---|---|
| **Host** | complete except what needs the radio attached |
| **Client** | Qt panel: connects, live status, rig control, RX audio. No TX audio, no PTT-off, no hotkey |
| **Route coverage** | **131 of 141** exact routes + all **18** prefix families |
| **Tests** | 8 host (ctest) + client selftest, all green |
| **Parity** | 25/25 with 5 listed deliberate divergences |
| **Verification** | 50/50 driven routes read back from the rig, 46/46 smoke, 25/25 parity, keypad and VFO-lock walked |
| **Radio** | never attached yet — everything below was built and proven against a simulator |

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
| PTT tail-wait → `/api/ptt/{off,toggle,unkey}` | **yes** |
| RX mute on TX is done client-side; host-side recording is not | **yes** |
| Client TX audio (`/ws/tx` framing exists and is tested) | no |
| Global PTT hotkey (F13) — platform code, Qt has no API for it | no |
| 19 `/api/admin/*` routes — user management is config-file-only today | no |
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

## 5. The tools, and why each fails closed

| tool | what it does | how it refuses to hurt the station |
|---|---|---|
| `tools/parity_check.py` | compares route **keys and types** against the reference host | allowlist **plus** a rule that any route whose final segment is not a read is refused. `on`/`toggle`/`tune` are not reads |
| `tools/walk_all_routes.py` | fires **state-changing** routes, including PTT | requires `/api/backend` to prove `simulated:true`. A 404 — which is what the reference host returns — is a refusal. **No `--force`** |
| `tools/coverage.py` | probes every reference route to measure what is implemented | same `/api/backend` guard |
| `--selftest` (host and client) | walks the startup path and exits | CI runs it under an external **timeout**: a hang is a failure |
| `--screenshot` (client) | renders the live window to a PNG | the only way to inspect a UI on a headless box |

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
