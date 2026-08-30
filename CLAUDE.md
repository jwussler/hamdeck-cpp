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

## ⚠️ C++ IS DECIDED. DO NOT RE-PITCH .NET.

The choice was made deliberately and is not up for review. NativeAOT, keeping the C# host,
"you could do that in .NET too" — all of it has been raised, considered and set aside. Raising
it again is noise, not diligence.

Judge proposals on whether they make the C++ build better, not on whether C++ was correct.

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

## Where this is going

⚠️ **A Raspberry Pi at the rig is the intended destination** — a small always-on box doing CAT
and audio, no VM, no hypervisor. That is the reason C++ was chosen at all, so it shapes decisions
now rather than later:

- **Build for ARM64 from day one.** Cross-compilation is free to set up at the start and painful
  to retrofit once x86 assumptions have spread. Add the ARM target to CI with the first binary,
  not the fiftieth.
- **No x86-only intrinsics, no assumptions about unaligned access**, and check any dependency
  actually has an ARM build before adopting it.
- ⚠️ **The CPU cost on a Pi is TLS and the tunnel, not the audio.** Audio is a memcpy at
  96 kB/s; the crypto is the load. Size the board for that. A Pi Zero 2 W or better is the
  realistic floor.
- The USB codec (TI PCM2903C) and the CP2105 CAT bridge both need `linux-modules-extra` on a
  stripped kernel — see CARRYOVER.md. Do not assume the Pi image ships `snd-usb-audio`.

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
