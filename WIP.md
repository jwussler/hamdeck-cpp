# HamDeck C++ — WIP

Live log. Folded into the real docs at the end. Started 08/30/2026.

> **This repo is public.** Nothing station-specific goes in it — no hostnames, no addresses,
> no VM ids, no tunnel details. CARRYOVER.md section 6 already says it for hostnames: *a
> hostname in a public repo points every install at that station.* The same applies to
> everything that identifies the site. Site detail lives in the gitignored `SITE.md`.
> This applies to **commit messages** too.

## 🛑 BLOCKER — do not push this repo yet

The working tree is clean of site detail, but **the git history is not.** Earlier commits of
`WIP.md`, and several of my own commit messages, contain the site's WAN address, a VPN peer
endpoint, internal addresses, VM ids and hostnames. Nothing has ever been pushed — there is
no remote — so nothing has leaked, and the fix is cheap while that stays true.

Two ways to clear it, both fine, Joe's call:

- **Squash to a single initial commit** before adding a remote. Loses the granular history;
  keeps it simple.
- **Rewrite just the affected commits** (`git filter-repo` over `WIP.md` and the messages).
  Keeps the history; more fiddly.

Until one of those is done: **no `git remote add`, no push.**

## Layout

- Code is developed on shack and built and run on a **disposable build VM**, which is also
  where the ALSA and serial work will run. `./sync.sh` rsyncs the tree there and builds it.
  The VM is the build host on purpose: it is the only place a green build means anything.
- A **reference host** runs the working .NET version against the real radio. It is read from
  for contracts and never written to.

## ⚠️ The rig hardware is single-instance

There is exactly one CAT bridge and one USB codec, and the reference host holds both. **Only
one machine can have the radio at a time**, so the build VM has no USB passthrough at all and
cannot take them by accident. The CAT bridge is a *dual* UART: one device enumerates two
serial ports, and passing it moves both together.

**What this means for the port:** everything that does not touch hardware — the REST surface,
WebSocket framing, auth, the build and CI — is built with the station on the air. Audio and
CAT need a **booked hardware window** with the station off the air. Exact commands are in
`SITE.md`.

**The cutover gate:** do not book a window until the real serial backend is written and
passing against the simulator, so the window is spent *measuring* rather than debugging
parser bugs a radio-free test would have caught.

## The road

141 routes in the C# `ApiServer.cs` dictionary across 46 groups — captured to
`reference/routes-csharp.txt`, with live JSON shapes in `reference/contracts.txt`. Most are
thin CAT wrappers, so this is not 141 hand-written handlers.

### Radio-free — the station stays on the air. Most of the work.

1. **Simulated rig** ✅
2. **Status cache + 200 ms poller** ✅
3. **Declarative route table** ✅ core operating set; the rest is mechanical
4. **Auth/session** ✅
5. **WebSocket** — `/ws` ✅ done; `/ws/tx` still to do (it needs somewhere to put the audio,
   so it lands with the ALSA work).
6. **systemd unit + CI that runs the binary** ✅

### Needs the radio.

7. **Real CAT serial** ✅ **written and tested without the radio** — see below. This is the
   cutover gate, and it is now met.
8. **ALSA via libasound** rather than `arecord`/`aplay` subprocesses — the actual point of
   the port — plus `/proc/asound` delay, adaptive buffering, PTT tail-wait, RX mute on TX.

## Findings from the live C# host — not in CARRYOVER.md

### The port split is load-bearing

The host listens on two ports and they are **not** interchangeable:

| port | binding | behaviour |
|---|---|---|
| control | local only | a LAN caller is refused outright |
| dashboard | LAN | `/api/health` and `/api/auth/status` anonymous; everything else 401 |

This is what makes "local" mean something, and it is how `/api/tune/amp` refuses every remote
caller (CARRYOVER.md section 2). **The C++ host binds the control server to the loopback
address so the guarantee is enforced by the kernel, not by a check somebody can forget to
write.** Locality is *which socket accepted the request* — never a header, which the caller
controls.

The first scaffold got this wrong and bound the control port to all interfaces. Caught by
comparing against the running C# host instead of trusting the doc.

### Deliberate divergence: `port` reports the real port
The C# host hardcodes `"port":5001` and answers that on the dashboard port too. Ours reports
the port actually bound, so the field can tell the listeners apart.

### ⚠️ A signal handler that swallows SIGTERM makes the service unkillable
The first scaffold's handler set an atomic flag nothing ever read, so the process caught the
signal and kept serving. `pkill` looked like it worked and did nothing; a stale build then
answered a probe and nearly passed as the new one. **A handler must actually stop the server,
or not exist.** This would have broken every `systemctl restart`.

## Design decision: API-first, no privileged client

The host stays **100% API-driven so other clients can be stood up**. Testable rules, not
aspirations:

- **No capability reachable except through a documented route.** If the panel can do it, a
  curl can do it. No hidden endpoint, no page that is the only way in.
- **No client is privileged.** The only asymmetry allowed is local vs remote, enforced by the
  socket.
- **The host holds no per-client UI state.** Clients are disposable and may drop and
  reconnect at will.
- **Capabilities are discoverable.** Absent capabilities must say so in their own status
  route — never a 200 that means "the route exists" (the `/api/record/start` lie).
- **Audio is part of the API contract**, not a side channel: `/ws` RX 22050/16/mono, `/ws/tx`
  TX 48000/16/mono.

## Auth

The hash format is a **compatibility contract**, not an implementation detail:

```
pbkdf2:<16-byte salt, lowercase hex>:<32-byte hash, lowercase hex>
PBKDF2-HMAC-SHA256, 350000 iterations
```

Change any parameter and every stored credential silently stops working.

**The test that matters is interop, not round-trip.** `tests/test_auth.cpp` verifies a hash
produced by an *independent* PBKDF2 implementation. A round-trip of our own hasher against
our own verifier would pass even if every parameter were wrong — it proves self-consistency,
not compatibility. It is in the suite second, for what little it is worth.

Matched to the C# host: anonymous set is `/api/health` + `/api/auth/status` only;
`allow_anonymous_status` is off; token from cookie, then `Bearer`, then `?token=`, in that
order; token in **Set-Cookie** never the body, `HttpOnly; SameSite=Strict`; 5 failures → 5
minute lockout; 500 ms delay on every rejection so "no such user" and "wrong password" cost
the same; 401 body byte-for-byte identical.

**The gate defaults to DENY and runs before routing.** `/api/ptt/on` did not exist yet and
already answered 401 rather than 404, so every route added from here is protected unless
somebody deliberately lists it anonymous. Open-unless-remembered is the shape that leaks,
because it fails open every time somebody forgets.

**Deliberately NOT implemented:** the C# host's transparent upgrade of legacy bare-SHA256
hashes on login. Whether any such hash still exists is unknown, and inventing a second
accepted hash format on a guess widens the auth surface for no reason.

**No Authelia, no SSO, no added login step** — Joe, 08/30/2026. The host's own session auth is
the boundary. Settled; do not raise it again.

## Route table, command queue, watchdog

### ⚠️ Request threads NEVER touch the serial port
The serial lock is not re-entrant across threads (CARRYOVER.md section 5), so exactly one
thread — the poller — speaks to the port. Handlers `Enqueue()` a CAT command and return; the
poller drains the queue at the top of each 200 ms cycle and reads state from the cache.
Queueing also orders commands, which matters for pairs like "set VFO then set frequency"
that are wrong if they interleave.

### ⚠️ `/api/ptt/off` is deliberately NOT implemented — it 404s
Unkeying must wait for the audio still queued in the ALSA buffer, or the tail of every
transmission is lost (CARRYOVER.md section 4a), and that wait needs the real device depth
from `/proc/asound`. An unkey that drops PTT immediately would *look* like it works and
quietly cut the end off every over — the exact bug that took a report from a net to find. A
404 is honest; a wrong unkey is not.

### TX watchdog — default 180 s
Lives next to the radio, on the poller thread (CARRYOVER.md section 4b). 0 disables. The test
asserts the **radio actually stopped**, read back through `TX;`, not that a trip counter
moved — a counter is a claim, `TX0;` is the outcome.

### The power cap is intentional
A local caller is capped lower than a remote one. It reads backwards; it is deliberate,
confirmed 08/30/2026. **Do not "fix" it.**

## The HTTP library had to change — cpp-httplib → civetweb

**cpp-httplib has no WebSocket support at all** — it defines the 426 status constant and
nothing else, and cannot hand off the socket. `/ws` therefore could never be served by it,
and moving `/ws` to a port of its own is not an option: the client connects to
`ws://host:<dashboard port>/ws`, so a separate port breaks every existing client.

civetweb (MIT, pinned v1.16) serves HTTP and WebSocket on one port. **The swap was done now,
at 5 routes, rather than later at 141.**

`src/http.h` is a thin wrapper so the route table does not know what is underneath. That swap
touched every route once; it should not happen twice.

After the swap the whole security boundary was re-verified rather than assumed: anonymous
`/api/health` and `/api/auth/status` still 200, `/api/status` and `/api/mode/lsb` still 401,
an unknown path still 401 (gate still ahead of routing), the control port still refused from
the LAN, all three token transports still work, and the 401 body is still byte-identical to
the C# host.

## RX audio — `/ws` done

Contract matched to the C# `AudioStreamer`:

- **config frame first**, as TEXT, before any binary frame:
  `{"type":"config","sample_rate":22050,"channels":1,"bits_per_sample":16}`. Without it the
  client cannot know how to interpret the bytes that follow.
- binary PCM, 22050 Hz / 16-bit / mono.
- **the auth gate runs on the WebSocket upgrade**, not just on REST. RX audio is somebody's
  operating activity; an unauthenticated upgrade is refused before a single frame is sent.
  Verified: no session → no handshake response at all.
- **one writer thread.** Every frame for every client goes out from a single thread, so
  frames can never interleave on a socket, and writes are locked per connection.
- **bounded queue, drop the OLDEST**, trimmed to 10 before each push — the same policy as
  the C# streamer.

Measured over the network with a dependency-free RFC6455 probe: **43.2 KiB/s, implied 22101
Hz** — against the 43.1 KiB/s CARRYOVER.md section 3 records from the live station.

### ⚠️ "Bounded" is easy to get backwards
Dropping the *newest* also bounds the queue and also passes a size check, while leaving the
listener permanently behind, playing an ever-later recording of the band. So the policy is
pulled out of the producer loop into `BoundedChunkQueue` and tested by identity, not size:
push 15 identifiable chunks, assert the survivors are chunks **5..14**. A size-only assertion
would pass either way.

### ⚠️ The first probe run "failed" and the server was innocent
The first `/ws` probe reported a BINARY first frame instead of the config frame. The bug was
in the probe: the handshake `recv` can pull the first WebSocket frame in with the headers,
and it was discarding the leftover. **Before believing a server is wrong, check the
instrument** — a broken measurement and a broken server look identical from the outside.

## Walking it again — the parity harness (`tools/parity_check.py`)

Answering "when we have a workable version, do we walk it again?": yes, and it is three
separate walks, only one of which needs the radio.

1. **API parity** — automated, radio-free, runs forever. `tools/parity_check.py` logs into
   both hosts (or tunnels to both local control ports, which need no session) and compares
   **keys and types**, not values. Currently 8/8 read-only routes match, with one listed
   deliberate divergence.
2. **Client compatibility** — point the real WPF client at the C++ host and check nothing
   greys out. The client probes at connect, so a missing capability shows up as a dead
   button rather than an error (CARRYOVER.md section 1).
3. **On-air** — the only one that needs the radio, and it needs *another operator*.
   CARRYOVER.md section 7 is explicit: **MONI cannot be captured from the host**, so there
   is no way to hear your own transmission from the host side. Audio quality and the PTT
   tail can only be confirmed by a second receiver or a net report.

### ⚠️ A parity walker that GETs every route would key the transmitter
Most of this API is state-changing and many of those routes are **GETs** — `/api/ptt/on`,
`/api/mode/cw`, `/api/tune/amp`. Walking all 141 against the live station would key the rig,
change the operating mode and retune the amp. CARRYOVER.md section 9 records that probing
with a control route once changed the operating mode mid-session with a human at the radio.

So the safety is **structural, not remembered**:

- an **allowlist**, copied from the reference host's own `ReadOnlyRoutes`;
- a second check of the final URL against state-changing patterns, so a future edit that adds
  a dangerous route to the allowlist still cannot fire one;
- **no `--all-routes` flag.** Adding one would defeat both checks. State-changing routes get
  compared against a *simulated* rig, never the station.

Proven before the tool was trusted: `/api/ptt/on`, `/api/mode/cw`, `/api/tune/amp` and
`/api/power/max` are all refused.

## Closed by the parity run

`/api/status/full` (18 fields) and `/api/meters` (5 fields) were 404 and are now implemented,
and the 404 body itself now matches the reference host, which names the path it could not
route.

⚠️ **The full set is polled every 5th cycle (~1s), not every 200 ms.** Reading ~16 extra CAT
commands five times a second would spend most of the serial budget on values that barely
move, and the serial port is single-threaded and shared with every command a request thread
queues. Meters *do* move fast, so they ride the fast loop. Fields not re-read on a cycle are
carried forward, so `/api/status/full` never flickers between real values and defaults.

## CI runs the binary, and a hang is a failure

`--selftest` walks the whole startup path — poller, audio, auth, both listeners — proves the
process actually answers a request, and exits. CI runs it **under `timeout 60`**: a selftest
that blocks forever looks exactly like one that is still working.

CI also asserts that an **unknown flag aborts**. Silently accepting a misspelled flag is how
a safety option gets quietly disabled.

`deploy/hamdeck-cpp.service` carries no hostname or credential; the admin hash comes from an
environment file outside the repo. `KillSignal=SIGTERM` with a 10 s stop timeout is
deliberate given the earlier unkillable-process bug.

## Serial CAT — written and proven with no radio attached

`SerialCat` is done and tested through a **pty with a fake rig on the far end**, which is the
whole reason it was written before the hardware window: every failure below is far cheaper to
find here than with the station off the air.

Proven (`tests/test_serial.cpp`, in ctest):

| | |
|---|---|
| probe | finds the CAT port by **asking with `ID;`**, skipping a dead candidate first |
| read | fixed-width reply parsed correctly |
| chunked | a reply dribbled out byte by byte is reassembled, not truncated at the first `read()` |
| trailing | returns only the first `;`-terminated reply, ignoring what follows |
| **no-slip** | a stale reply is never returned as the answer to a later command |
| timeout | an unanswered command returns `nullopt` in ~249 ms and **does not hang** |
| exclusive | a second opener is refused |
| baud | an unsupported rate is refused, not silently substituted |

### ⚠️ The reply-slip bug is the one that would have been nasty
Without flushing input before each command, a leftover reply from a previous timed-out
command comes back as the answer to *this* one — and then every subsequent reply is off by
one, **each individually plausible**. A frequency that is really the mode, a mode that is
really the power. It would look like a flaky radio.

### ⚠️ Exclusive access, failing closed
The .NET host and this one both want the same port. Two processes interleaving commands on
one CAT link produce replies attributed to the wrong command — a fault that looks like the
radio and is nearly impossible to diagnose from either side. `flock(LOCK_EX|LOCK_NB)`, and a
refusal to share.

### ⚠️ The default is the SIMULATOR, and there is no fallback to it
Talking to the radio must be asked for: `HAMDECK_CAT_DEVICE=<path>|auto`. A host that hunted
for a serial port on startup would grab the CAT link the moment it ran anywhere near the
station — on a laptop, in CI, on a box meant only for building.

And when a device **is** named and cannot be opened, the host **exits 1**. It does not fall
back to the simulator. A host that comes up looking healthy while reporting a rig it cannot
reach is worse than one that refuses to start — that is the `/api/record/start` lie in a
more dangerous place.

## 🎯 THE CUTOVER GATE IS MET

Everything that can be built without the radio is built. The next step is the hardware
window, and it should be spent on what only the radio can show: `/proc/asound` delay,
adaptive buffering, the PTT tail, and RX mute on TX.

Order for the window, so the risky part comes last:
1. `HAMDECK_CAT_DEVICE=auto ./hamdeck-host --selftest` — proves the port, the probe and the
   identity in seconds, keying nothing.
2. `/api/status` against the real rig; compare with the reference host's last known values.
3. Only then the audio work.

## ⚠️ A test that cannot fail is not a test

The obvious staleness check — `SIGSTOP` the process, re-query — is worthless. It freezes the
HTTP server too, so the poller refreshes the cache the instant the process resumes and the
answer comes back fresh. It looks like a pass and measures nothing. Same shape as the
byte-count latency estimate in CARRYOVER.md section 3, which read ~0 in steady state while
435 ms sat in the ALSA buffer: **an estimate whose failure mode is zero looks exactly like a
working measurement.**

The real test stops the poller and watches the cache age: `99ms` → `2200ms stale=true`
against a 1500 ms threshold.

## Done

- CMake + Ninja, C++23, cpp-httplib pinned, OpenSSL for PBKDF2.
- `/api/health`, `/api/status`, `/api/auth/{login,logout,status}`, and the core operating
  routes: mode, vfo, split, lock, power, freq reads.
- 3 tests under ctest: staleness, auth, watchdog.
- Verified by driving the simulated rig through the API over the network and reading state
  back — not from localhost.
