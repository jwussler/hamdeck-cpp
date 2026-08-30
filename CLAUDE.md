# HamDeck — C++ port

A C++ rewrite of the HamDeck host (and, later, a Qt client). The shipping version is C#
at `jwussler/HamDeck`; this is a parallel implementation, not a replacement, and the
**HTTP/WebSocket API is the contract between them**. Keep it identical or the existing
Windows client stops working.

## Read these first

- **`CARRYOVER.md`** (this repo) — the API surface, the audio chain with measured numbers,
  the three PTT traps, and a list of things that are **not possible** so they don't get
  retried. Written from measurements on the live station, not recollection.
- **`~/hamdeck-site/brand/BRAND.md`** — the visual identity. **Anything with a user
  interface, an icon, a page or a banner follows it.** Exact colour tokens, the type
  pairing, the mark rules, and how to apply each. Use the tokens verbatim; do not invent
  colours or substitute typefaces. If it doesn't cover what you need, say so rather than
  guessing.

## Decisions already made — don't relitigate without a reason

- **The host is the target.** It's where C++ earns its keep: audio, timing, serial, and a
  small binary that could live on modest hardware. The client is optional and secondary.
- **Qt Quick (QML), not Qt Widgets**, if a GUI is built. The panel is custom-drawn dark
  instrumentation; Widgets fights that, QML is designed for it.
- **Keep `core/` free of any UI headers.** Protocol, radio state, audio and the PTT
  abstraction must not include Qt. That is what makes swapping the front end a weekend
  rather than a rewrite — and it's the exact discipline that saved the C# client.
- Suggested libraries, all OSI-approved so code signing stays possible: miniaudio (audio),
  cpp-httplib (REST), IXWebSocket, nlohmann/json, Dear ImGui if a lighter UI is wanted.

## Non-negotiables

- ⚠️ **Never probe a live rig with a control route.** Reading is fine; `/api/mode/usb`
  changed the operating mode mid-session once. Use `/api/health` — the only route needing
  no session.
- ⚠️ **The transmit watchdog lives next to the radio**, never in the client. A client-side
  timeout protects nothing when the client is the thing that died.
- ⚠️ **Unkeying waits for queued audio.** Ask the kernel
  (`/proc/asound/<card>/pcm<n>p/sub0/status` → `delay`), never infer it from byte counts —
  that estimate is always ≈0 in steady state and reads exactly like a working measurement.
- ⚠️ **Never feed the operator their own delayed audio.** Mute RX while keyed.

## House style

MIT licensed; every dependency must stay OSI-approved. Measure before claiming — every real
fix in this project came from a measurement, not a reading of the code.
