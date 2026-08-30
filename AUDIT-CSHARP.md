# Walking the C# app, bit by bit

**Why this file exists:** gaps were being found one at a time, by the operator, in use — the
PTT hotkey that never keyed, the TGXL that never transmitted, direct entry that was never
ported. Each cost a rebuild and a reinstall. This is the whole surface of the reference app,
written down, so the remaining work is a list to walk rather than a series of surprises.

Reference: `jwussler/HamDeck` (C#/WPF host + `HamDeck.Remote` client).
Audited 08/30/2026 against this repo's host and QML client.

---

## 1. The host is ROUTE-COMPLETE. That was measured, not assumed.

Every route in the C# `ApiServer.cs` route table (141) resolves on the C++ host. The 28 that
looked missing from a text diff — `/api/att/toggle`, `/api/mute-all/*`, `/api/xit/*`,
`/api/vox/*`, `/api/rit/on|off|toggle`, `/api/ant/1..3`, `/api/nb|nr/on|off` — are all served
by prefix routes and answer 200.

⚠️ Probed against the **simulated** host, never the station: most of those routes change state
and several key the transmitter. The probe was checked against a route that does not exist
(`/api/definitely-not-a-route` → 404) before its result was believed — a probe that cannot
report a miss is not a probe.

**So the gaps are not routes. They are SERVICES the host does not run, and CONTROLS the panel
does not offer.**

---

## 2. Host services

| service | C# | here | decision |
|---|---|---|---|
| `Tuners.TgxlTuner` | 15 W CW carrier + autotune | ✅ **ported 08/30** | see WIP §8e — it never keyed the rig before |
| `Tuners.AmpTuner` | 20 W CW, **10 s carrier**, ends at 100 W | ❌ **stub** — `/api/tune/amp` says "not configured" | **portable now**, local-only. Next. |
| `Keyers.CwKeyer` | send CW text, 5 CW memories | ❌ stub — `/api/cw/*` reports `available:false` | portable; needs the CAT verbs checking against the manual |
| `TcpCatProxy` | CAT on **localhost:4532** so N1MM shares the port through the same lock | ❌ not ported | **wanted** — this is what removes the virtual-serial-port splitter |
| `WaveLogServer` | WaveLogGate: HTTP 54321 for QSY from the bandmap, WS 54322 status, Wavelog API posting | ❌ not ported | **wanted if the log is used** |
| `DxClusterClient` | polls a JSON spot API, tune-to-spot | ❌ absent | `/api/cluster/spots` **404s on the reference host too** — matching is correct |
| `FlexKnobController` | USB encoder, two protocols | ❌ absent | hardware not on this host |
| `KmtronicService` | UDP 8-channel relay | ❌ absent | hardware not on this host |
| `AudioRecorder` | ring buffer + continuous record | ✅ ported | |
| `AudioStreamer` / `AudioTransmitter` | RX/TX WebSocket audio | ✅ ported | measured: 93.8 KiB/s TX, 501 ms device delay |
| `AuthService` | sessions, lockout, admin | ✅ ported, plus `/api/admin/*` | |
| `RadioController` | CAT + poller | ✅ ported, and **six CAT bugs only hardware found** (WIP §4b) | |

---

## 3. The panel: what the C# client offers and this one does not

Derived by diffing the routes each client calls, then removing false positives (this panel
builds `/api/ant/`, `/api/width/`, `/api/band/` and `/api/<x>/toggle` dynamically, so those
were never missing).

### Done 08/30/2026
- ✅ **Keyboard direct entry** — click the frequency, type it, Enter. `14.200`, `14200`,
  `14`, `7.185.000`, `14,200,000` all resolve; 30 kHz–75 MHz range check; a bad parse
  **refuses** rather than sending, because `/api/freq/set` moves the MODE too. The parser is
  in C++ and runs the reference implementation's **own 22 test cases** (`ctest -R freq`).
- ✅ **System-wide PTT key** — `RegisterHotKey`, press-to-toggle, with `MOD_NOREPEAT` and the
  bare-key fallback, and error 1409 reported as "another program holds it".

### Still missing, roughly in the order they are worth doing
| control | route | note |
|---|---|---|
| **Recording + replay buffer** | `/api/record/toggle`, `/replay`, `/status` | ⚠️ the HOST already implements this and the panel cannot reach it |
| **VFO lock** | `/api/vfo-lock/toggle` | a safety feature of this host, invisible in the panel |
| **Rig CAT lock** | `/api/toggle/lock` | different from the software VFO lock — see WIP §6 |
| **Quick split** | `/api/quick-split` | one press; compound sequence already on the host |
| **VFO copy A→B** | `/api/vfo-copy/a2b` | |
| **Preamp / IPO** | `/api/preamp/cycle` | |
| **RIT and XIT on/off** | `/api/rit/toggle`, `/api/xit/toggle` | panel has RIT up/down/clear only |
| **Mute, mute-sub, mute-all** | `/api/mute*/toggle` | |
| **RX antenna** | `/api/rxant/1`, `/api/ant/rx/toggle` | |
| **Diversity** | `/api/diversity/toggle` | status field already carried |
| **Memory recall** | `/api/memory/recall/{m}` | |
| **Presets** | `/api/preset/`, `/api/admin/presets` | host route exists for hardware this host lacks |
| **Voice memories** | `/api/voice/play/{n}`, `/stop`, `/status` | |
| **SSB out level** | `/api/ssb-out-level/get|set` | |
| **Remote TX** | `/api/remote-tx/on|off|gain|status` | |
| **Rig internal ATU** | `/api/tune` | ⚠️ the WRONG tuner for this station — label it clearly if added |
| **Mouse wheel tunes the VFO** | `/api/step/{hz}/{dir}` | C# `OnFreqWheel`; wheel over the readout |
| **Step size selector** | `/api/step/{hz}/{dir}` | C# has a row of step sizes + ◀ ▶; this panel has ±1 kHz only |

---

## 4. Rules taken from the reference while walking it

Kept here because they are the sort of thing that is invisible until it costs an evening.

- **The tuner drives the RADIO, not just the tuner.** No carrier, no tune (WIP §8e).
- **A global hotkey is press-to-toggle**, because `RegisterHotKey` has no key-up. Hold-to-talk
  needs a `WH_KEYBOARD_LL` hook that sees every keystroke on the machine — an antivirus flag
  and a privacy consideration, and the reference deliberately does not do it.
- **`MOD_NOREPEAT`, with a fallback.** Without it a held key flaps the transmitter; with it, a
  bare key with no modifier is rejected on some systems, so register again without it rather
  than leaving the operator a dead key.
- **Register the native filter ONCE.** Two filters deliver one press twice, a toggle goes on
  and straight back off, and the key looks broken.
- **Alt+Space is never offered** as a global hotkey: it is the Windows system-menu accelerator.
- **A frequency that does not parse is REFUSED**, not rounded to something plausible —
  `/api/freq/set` moves the mode with it.
- **`/api/tune` is the rig's internal ATU and is the wrong tuner here.** Each tuner names
  itself in its reply so a confirmation cannot just say "tuning".
