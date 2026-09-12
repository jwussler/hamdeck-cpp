# ARCHIVE — finished sections moved out of WIP.md

⚠️ **This is finished work, not the working list.** Open items live in `WIP.md`.

Moved 2026-09-12 because the working file had grown to 1813 lines and a bloated WIP HIDES open items. Only sections whose own heading said DONE/FIXED/COMPLETE/RESOLVED/✅ were moved; anything ambiguous was kept.

---

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

