# HamDeck → C++ : carry-over

Everything a C++ port needs that is **not** obvious from the C# source, written the night the
.NET version was made to work properly (08/30/2026). Read this before writing any code.

The existing project is `jwussler/HamDeck`, branch `linux-port`. Two halves:

- **host** — talks to the radio over CAT serial, owns the audio devices, serves a REST + WebSocket
  API. Runs on the VM (`ssh <reference-host>`, [lan-host]) as `the host service`.
- **client** — WPF panel on Windows. Talks only to the host's API; it never opens a serial port.

**A C++ port should target the HOST first.** It is the half where the language argument has any
force (audio, timing, serial), it has no UI, and the existing Windows client will keep working
against it unchanged as long as the API is preserved.

---

## 1. THE THING TO GET RIGHT FIRST: the two hosts are not the same

`Linux/Program.cs` builds the API server with only
`radio, recorder, config, tgxl, amp, auth, cwKeyer`. So on the Linux host these are **null** and
their routes **404**: the KMTronic RX antenna switch, the DX cluster, session stats, the voice
keyer. The Windows host builds all of them.

The client **probes** at connect and greys out what is missing. If you port the host, either
preserve that behaviour or the client will show dead buttons.

⚠️ **`/api/record/start` LIES on the Linux build.** `Start()` sets `IsRecording = false` and the
route still answers `{"status":"ok","recording":true}`. A 200 there means the route exists, not
that anything is recording. The only honest signal is `/api/record/status` → `file_recording`.
**Do not reproduce this.** If a capability is absent, say so in its status route.

---

## 2. API surface the client depends on

184 routes exist; the client uses 44 capability groups. The ones that matter:

| route | notes |
|---|---|
| `GET /api/health` | **the only route with no session.** Carries `rig_connected`, `tgxl_tuning`, `amp_tuning`. Use it for liveness and tuner status. |
| `GET /api/status` | the panel's main poll — freq, mode, vfo, power, tx, split, `cache_age_ms`, `stale` |
| `GET /api/status/full` | ant, rxant, nb, nr, notch, lock, preamp, att, agc, vox, comp, mon, rit, rit_offset, xit, freq_b, rf_gain |
| `GET /api/meters` | s_meter, swr, alc, power |
| `POST /api/auth/login` | returns the session as a **Set-Cookie** (`hamdeck_session`), *not* in the JSON body |
| `GET /ws` | RX audio, **22050 Hz / 16-bit / mono**, binary frames + one JSON `config` frame |
| `GET /ws/tx` | TX audio, **48000 Hz / 16-bit / mono**, binary frames from the client |
| `/api/ptt/{on,off,toggle}` | see §4 — unkeying is not instant |

Auth: session cookie, also accepted as `?token=` or `Bearer`. Since v3.4.14 status **and** RX
audio require a session. `web_admin_only=true` makes `/` serve the admin page and 404s the old
browser rig UI.

⚠️ **`/api/tune` is the rig's INTERNAL ATU and is the wrong tuner for this station.** The right
one is `/api/tune/tgxl`. Keep them separate and name them in any confirmation.
⚠️ **`/api/tune/amp` refuses every remote caller** (`AmpTuneOrDeny(isLocal)`). Do not expose it
remotely; a button that always errors is worse than a missing one.

---

## 3. Audio — the whole reason a C++ port is interesting

Chain on Linux, both directions through **subprocess pipes**, not libasound:

```
RX:  rig → USB codec → arecord → host → /ws        → client → speakers
TX:  client mic → /ws/tx → host → aplay → USB codec → rig mic input
```

Device is `hw:CODEC,0` (TI PCM2903C). Capture supports 8000–48000 Hz; **playback only
32000–48000**. That asymmetry is why RX is 22050 and TX must be 48000.

### The measurements that matter (all real, taken on the live station)

| what | value |
|---|---|
| aplay default buffer | **~500 ms** — and it never drains, because we feed at exactly real time |
| after `--buffer-size` | **239 ms** measured |
| steady-state TX latency | **202–233 ms** |
| RX stream rate | 43.1 KiB/s, exactly nominal for 22050/16/mono |

### ⚠️ How to measure latency — this is the single most valuable thing here

```
/proc/asound/<card>/pcm<n>p/sub0/status   →   delay : <frames>
```

`delay` is **exactly** what is queued on the device, right now, from the kernel.
`/proc/asound/CODEC` is a symlink to the card, so the path is derivable from `hw:CODEC,0`.
`state: XRUN` means an underrun happened — the cushion was too thin.

**Do not infer latency from byte counts.** The .NET version did: written-bytes-as-duration minus
elapsed time. In steady state that is **always ≈ 0**, because the sender sends at real time — so
it reported "nothing queued" while 435 ms sat in ALSA. An estimate whose failure mode is zero
looks exactly like a working measurement. That bug cut the end off every transmission and it
took a report from a net to find.

### Adaptive buffering (implemented, keep the design)

Give the device a generous buffer and control the **fill level** yourself:

- start target 150 ms, floor 80, ceiling 600
- **XRUN → target += 60 ms**
- 30 s with no XRUN → **target −= 20 ms**
- when queued > target + 60 ms **and the rig is not keyed**, drop the incoming frame

⚠️ **Trim only between overs.** Dropping audio mid-transmission is audible; between overs it
costs nothing, and with the mic held open there is always idle time. The effect is that every
transmission *starts* at the target latency however far the link drifted. This is what makes a
cell link usable.

---

## 4. PTT — three separate traps

**a. Unkeying must wait for the queued audio.** Otherwise the tail of every transmission is lost:
the last fraction of a second is still in the pipe and the device buffer, and once PTT is gone it
is never transmitted. Wait the **kernel `delay` at the moment of unkey**, then drop PTT.
Hard-cap it (1200 ms). Nothing is worth an open carrier.

Wait the depth **at that moment**, not "until empty" — the mic stays open, so an until-empty loop
never terminates.

**b. The transmit watchdog must live next to the radio.** `ptt_timeout_seconds`, default 180.
A browser-side or client-side timeout protects nothing: close the tab, sleep the laptop or lose
the link while keyed and the rig stays keyed with nobody watching. The Linux host shipped without
this for months because it lived only in the WPF host's window class.

**c. Never feed the operator their own delayed audio.** Hearing yourself back at the round-trip
delay is **delayed auditory feedback** — it disrupts speech so reliably that speech labs use it
deliberately. The operator will slur, hear themselves doing it, and report the link as broken.
**Mute RX while the rig is keyed**, driven off the rig's own `tx` state so every PTT source
behaves alike, and **drop what queued** on unmute so they come back live rather than replaying.

---

## 5. Host-side things that were missing and had to be added

Both lived only in the WPF host and were never ported to Linux. Check for more of this shape.

- **Status cache refresh.** `/api/status` is served entirely from cached `Last*` values and never
  touches the serial port from a request thread (the serial lock is not re-entrant across
  threads). The WPF app refreshed them every ~200 ms; the Linux host had no equivalent, so
  `/api/status` served a frequency **3.6 hours stale** and a `tx:true` left over from a tune while
  the rig was receiving. A dedicated 200 ms thread does it now, skipped while the CAT proxy is
  active.
- **Transmit watchdog** — see §4b.

---

## 6. Client-side traps (if you port the client too)

- **`SendAsync` on a WebSocket must be serialised and awaited.** Overlapping sends are rejected
  outright. In C++ the equivalent is: one writer, a queue, bounded, **drop the oldest** when full.
  An unbounded queue silently becomes latency.
- **Audio callback buffers are reused by the audio API.** Copy before queuing.
- **Store audio devices by NAME, never index.** Indices shift when USB devices come and go; that
  is what produced `BadDeviceId` and a dead microphone.
- **Default to the system default device**, never index 0. On Windows that is `WAVE_MAPPER` (-1);
  index 0 is arbitrary and is out of range when there are no devices.
- **A global PTT hotkey via `RegisterHotKey` gets key-DOWN only** — press-to-toggle, not
  hold-to-talk. Hold needs a `WH_KEYBOARD_LL` hook, which sees every keystroke on the machine.
  `MOD_NOREPEAT` is mandatory or a held key flaps the transmitter. **Alt+Space is unusable** (it
  is the window system-menu accelerator). **F13 is ideal** — no physical keyboard sends it, so
  nothing conflicts; a programmable keyboard or footswitch can be mapped to it.
- **Never let a window open larger than the work area.** Clamp to the screen and re-centre, or a
  window taller than the display puts its title bar off-screen and the app cannot be reached at
  all.
- **Store settings outside the install directory** so updates cannot overwrite them, and put no
  password in them. **Never ship a default host** — a hostname in a public repo points every
  install at that station.

---

## 7. Things that are NOT possible, so nobody retries them

- **MONI cannot be captured from the host.** Enabling MONI and recording `/ws` for 120 s produced
  uniform band noise with no transmission in it. The monitor goes to the headphone path, not the
  rear USB audio. **There is no way to hear the transmission from the host side** — verification
  needs a second receiver or a net report.
- **WPF cannot be cross-compiled.** `Microsoft.NET.Sdk.WindowsDesktop` does not exist for Linux.
  (Irrelevant to a C++ client, but it is why the .NET client is built on a Windows CI runner.)
- **Amp tune is local-only** — see §2.

---

## 8. Build and verify — the discipline that was missing

A green build proves the code compiles. It does not prove the program starts. The .NET client
shipped a release that could not launch at all while every test passed.

- **run the binary in CI**, not just the tests. The client has a `--selftest` that walks the
  startup path and exits; treat a hang as a failure too.
- **verify the artifact, not the build**: check the PE machine type is really ARM64, check icons
  are really embedded, download the published asset back and check the byte count.
- **measure, then change, then re-measure.** Every real fix here came from a measurement:
  `/proc/asound` delay, `cache_age_ms`, PE machine type, `RT_ICON` counts.

---

## 9. Environment

- host: `ssh <reference-host>` (the VM, [lan-host]). Service `the host service`. Repo `~/HamDeck`.
- ⚠️ **the VM's branch has diverged from origin.** It carries ~408 lines of working ALSA audio,
  the status-cache fix, the watchdog and the adaptive latency work that are **not on GitHub**;
  origin has a parallel implementation plus `CODE_OF_CONDUCT.md`/`SECURITY.md`. **Do not resolve
  this by force-pushing either side.** Bundles in `~/backups/hamdeck/` on shack.
- the VM has **no GitHub credentials for HTTPS**; it pushes via a deploy key
  (`~/.ssh/github_hamdeck`). shack's token lacks `workflow` scope, so anything touching
  `.github/workflows/**` must be pushed from the VM.
- rig: Yaesu FTDX-101MP, CAT on `/dev/ttyUSB0` at 38400. Probe safely with `ID;` → `ID0682;`.
  **Never probe with a control route** — doing that once changed the operating mode mid-session.
