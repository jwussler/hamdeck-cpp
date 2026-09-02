# HamDeck — C++ port

A C++ rewrite of the HamDeck host (and, later, a Qt client). The shipping version is C#
at `jwussler/HamDeck`; this is a parallel implementation, not a replacement, and the
**HTTP/WebSocket API is the contract between them**. Keep it identical or the existing
Windows client stops working.

## Read these first

- **`docs/internal/CARRYOVER.md`** (this repo) — the API surface, the audio chain with measured numbers,
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
- ⚠️ **Anything with a UI is resolution-aware from the first line.** The panel must be readable
  on a 4K monitor and unclipped on a 1024x600 netbook, and those are two different mechanisms:
  a single density scale (`Theme.u()`/`f()`, from `Backend::uiScale`) *and* reflow against the
  width actually available (`Theme.cols()`). No unscaled pixel constants. Prove it with
  `--check-resolutions`, which measures every key at seven screen sizes — and look at the
  PNGs it writes. See docs/internal/WIP.md §8d, including the four ways that walk passed while measuring
  nothing.
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
  stripped kernel — see docs/internal/CARRYOVER.md. Do not assume the Pi image ships `snd-usb-audio`.

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

## ⚠️ RULES EARNED ON 08/31/2026 — the night the client first transmitted

Six bugs stood between "it compiles" and "a voice on the air". **Not one was a language or a
build problem, and every single one looked healthy to the checks that existed.** Full account in
`docs/internal/WIP.md` §8f–§8h; these are the rules that came out of it.

### Counting is not checking
Frames accepted, `hw_ptr` advancing at 48 kHz, zero drops, a queue behaving — **all of it reads
exactly the same whether the audio is a voice or digital silence.** A muted microphone produces
perfectly formed silence at precisely the right rate. If a number cannot distinguish working
from broken, it is not a measurement, and reporting it as one wastes an operator's night.
`tx_peak` exists for this reason, and it **decays**: a high-water mark cannot show a gain being
turned DOWN, which is the entire job.

### A control no test TOUCHES is a control nobody has tested
`qml_selftest` loads the QML and `--check-resolutions` measures geometry. Every slider in the
panel had **zero height** and swallowed no mouse events, and both stayed green for the life of
the project, because a zero-height item lays out and paints perfectly well. `tests_knob.cpp`
drags one with synthetic mouse events. Do that for any control that matters.

### Comparing route INVENTORIES is not comparing behaviour
`docs/internal/AUDIT-CSHARP.md` ticked `/api/remote-tx/on` because the route existed. It answered `200` and
changed nothing, and the status route beside it **invented all three of its fields** so the two
agreed with each other. ⚠️ **The test that catches this: call the route, then read the radio
back through something that is NOT the route under test.**

### A confident wrong answer is worse than no answer — it gets believed
`/api/remote-tx/status` reported the rig's TX flag as `mod_source_rear` and hardcoded the rest,
behind a comment claiming it read straight through. It said MIC while the radio was already on
REAR/USB, so a **correct** write was reported as a failure and the search went to the wrong end
of the chain. When a read fails, say `null`. Never fall back to a plausible value.

### ⚠️ NEVER SHIP A FIX NOTHING HERE CAN TEST
It happened twice in one night and the operator found both:
- **0.1.10** "fixed" the sliders by correcting a real binding loop that was not the cause.
- **0.1.12** "added" the icon with a `.rc` that CMake **silently never compiled**
  (`project(... LANGUAGES CXX)` — no RC language, no warning, green build).

Flagging "I could not verify this" in the message is not a substitute for verifying it. If it
cannot be tested here, build the thing that tests it — `tests_knob.cpp` and
`check_exe_icon.py` both exist because of this, and each took minutes.

### A gate is not a gate until it FAILS on demand
Every gate in this repo was verified by reintroducing its bug and watching it fail, then
restoring it and watching it pass. A check that has only ever passed is a check nobody has
tested. It is the same rule as the one above, applied to the test itself.

### Two mechanisms are two tests
The `--selftest` icon check covers the WINDOW icon (qrc + `setWindowIcon`). The taskbar,
shortcut and Explorer icon comes from the **exe's embedded resource** — a completely different
path. A check on one says nothing about the other, and reporting it as covering both is how
0.1.12 shipped blank.

### Safety belongs next to the radio
The transmit watchdog, the disconnect power reset and the MOD SOURCE restore all live in the
HOST. ⚠️ **A client-side safeguard protects nothing when the client is the thing that died** —
and a dropped link, a crash and a clean quit all look the same from here, which is exactly why
the `/ws/tx` close callback is the right hook.

### CAT menu writes need 50 ms between them
Sent back to back the rig takes the first and ignores the rest, **silently**. The reference host
has three explicit sleeps in `EnableRemoteTx`; nobody writes those for fun. Port the timing, not
just the commands.

## House style

MIT licensed; every dependency must stay OSI-approved. Measure before claiming — every real
fix in this project came from a measurement, not a reading of the code.
