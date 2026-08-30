# HamDeck C++ — WIP

Live log. Folded into the real docs at the end. Started 08/30/2026.

## Where the work happens

| | |
|---|---|
| code (this repo) | `shack:/home/ubuntu/hamdeck-cpp` |
| build/run VM | **the VM `hamdeck-cpp`**, [lan-host], `ssh <build-host>` |
| public name | **`[build-host]`** — chosen 08/30/2026, not yet published |
| reference host | the VM `hamdeck`, [lan-host], `ssh <reference-host>` — the working .NET host, **do not disturb** |

### ⚠️ `[reference-host]` was already taken
It resolves to **[lan-host] = the VM**, the existing .NET host, in public DNS and in
shack's `~/.ssh/config`. Do not reuse it for the C++ host. `[build-host]` is the C++ host;
it is free in DNS and absent from the cloudflared ingress.

Also taken, checked the same way: `[lan-host]` = `tunnel.wa0o.com`. First free LAN
address was `.64`.

## the VM — built 08/30/2026

Ubuntu 24.04.4 LTS from `noble-server-cloudimg-amd64.img`, 4 cores / 4 GB / 40 GB on
`local-zfs`, static `[lan-host]/24` gw `.1`, cloud-init user `ubuntu`, key
`shackvm-admin` (same `~/.ssh/vm_admin` shack uses for pve and the VM).

`onboot: 0` **deliberately** — this is a disposable box and it must never come up on its
own and race the VM for hardware.

Toolchain installed and each piece proven by running it, not by reading the install log:

- g++ 13.3.0 — compiled and ran a `-std=c++23` binary
- CMake 3.28.3, Ninja 1.11.1
- libasound2-dev 1.2.11 — linked `-lasound` and called `snd_asoundlib_version()`
- qemu-guest-agent — `qm <op> <vmid> ping` answers from pve

Destroy/rebuild: `ssh <hypervisor> 'qm <op> <vmid> && qm <op> <vmid>'`. Nothing on it is precious.

## ⚠️ THE HARDWARE IS SINGLE-INSTANCE — the constraint that shapes the schedule

the VM holds both rig devices by vendor:product passthrough:

```
usb0: host=10c4:ea70   Silicon Labs CP2105 Dual UART Bridge   -> CAT, /dev/ttyUSB0
usb1: host=08bb:29c3   TI PCM2903C Audio CODEC                -> hw:CODEC,0
```

`lsusb` on pve shows **exactly one of each**. There is no second CAT interface and no
second codec. the VM therefore **cannot talk to the radio while the VM is running**, and
the VM was created with no USB passthrough at all so it cannot take them by accident.

The CP2105 is a *dual* bridge: one physical device enumerates `/dev/ttyUSB0`
(`ID_USB_INTERFACE_NUM=00`) and `/dev/ttyUSB1` (`=01`), serial `[serial]`. Passing the
device moves **both** ports together.

**What this means for the port:** everything that does not touch hardware — the REST
surface, the WebSocket framing, auth/session, the build and CI, the health route — gets
built and proven on the VM with the VM untouched and the station on the air. The audio and
CAT work, which is the whole reason the port is interesting, needs a **booked hardware
window** with the station off the air.

### The hardware window — decided 08/30/2026

Joe's call: **just shut the VM down to release the devices.** Stopping the VM frees the
physical USB device on the pve host; 102's config keeps its `usb0`/`usb1` lines untouched,
so giving the radio back is only a `qm start`.

```
# take the radio
ssh <reference-host> 'sudo systemctl stop the host service'
ssh <hypervisor>     'qm <op> <vmid>'
ssh <hypervisor>     'qm <op> <vmid> -usb0 host=10c4:ea70 -usb1 host=08bb:29c3'   # once, then it sticks
ssh <hypervisor>     'qm <op> <vmid>'      # or qm reboot 105 if it is already up

# give the radio back — always, at the end of every session
ssh <hypervisor>     'qm <op> <vmid> && qm <op> <vmid> -delete usb0 -delete usb1'
ssh <hypervisor>     'qm <op> <vmid>'
ssh <reference-host> 'systemctl is-active the host service'    # prove it, do not assume
```

Verify the handover on 105 before trusting it, rather than assuming the passthrough took:
`lsusb | grep -E "10c4:ea70|08bb:29c3"`, `ls /dev/ttyUSB*`, `aplay -l`.

⚠️ **the VM is `onboot: 1`.** If the pve host reboots during a hardware window it will
bring 102 back up and the two VMs will fight over the devices. Left as-is on purpose — 102
auto-starting is correct for a station — but it is the one way this procedure bites.

⚠️ Never leave the radio detached from the VM unattended. The station is off the air for
the whole window.

## Findings from the live C# host — not in CARRYOVER.md

### The port split is load-bearing (measured on the VM, 08/30/2026)

CARRYOVER.md never says why the host listens on two ports. It matters:

| port | binding | behaviour |
|---|---|---|
| **5001** | local only | control API. A LAN caller is refused outright: *"The control API only serves this machine. Use the dashboard port with a session, or set `api_bind_lan`."* |
| **5002** | LAN | dashboard. `/api/health` answers with no session; everything else needs one. |

This is what makes "local" mean something, and it is how `/api/tune/amp` can refuse every
remote caller (CARRYOVER.md section 2). **The C++ host binds the control server to
`127.0.0.1` so the guarantee is enforced by the kernel, not by a check somebody can forget
to write.** Locality is *which socket accepted the request* — never a header, since
`X-Forwarded-For` and friends are caller-controlled.

The first scaffold got this wrong: it bound `0.0.0.0:5001`, publishing the control port to
the whole LAN. Caught by comparing against the running C# host instead of trusting the doc.

### Deliberate divergence: `port` reports the real port
The C# host hardcodes `"port":5001` and answers that on 5002 as well — verified. Ours
reports the port actually bound, so the field can tell two listeners apart.

### ⚠️ A signal handler that swallows SIGTERM makes the service unkillable
The first scaffold's `SIGTERM` handler set an atomic flag that nothing ever read, so the
process caught the signal and kept serving. `pkill` looked like it worked and did nothing;
a stale build answered a later probe and nearly passed as the new one. **Any handler must
actually stop the server, or not exist.** This would have broken every `systemctl restart`.

## Design decision: API-first, no privileged client (08/30/2026)

Joe's call — the host stays **100% API-driven so other clients can be stood up**. Concretely
that means, and these are testable rules, not aspirations:

- **No capability reachable except through a documented route.** If the WPF panel can do it,
  a curl can do it. No hidden endpoint, no server-rendered page that is the only way in.
- **No client is privileged.** The browser UI is just another consumer of the same API. The
  only asymmetry allowed is `local vs remote`, and it is enforced by the socket (above).
- **The host holds no per-client UI state.** State belongs to the radio; clients are
  disposable and may connect, drop and reconnect at will.
- **Capabilities are discoverable.** The existing client already probes and greys out what is
  missing (CARRYOVER.md section 1). Absent capabilities must say so in their own status
  route — never a 200 that means "the route exists" (the `/api/record/start` lie).
- **Audio is part of the API contract**, not a side channel: `/ws` RX 22050/16/mono, `/ws/tx`
  TX 48000/16/mono.

## Done so far

- CMake + Ninja project, C++23, cpp-httplib pinned at v0.18.3 via FetchContent.
- `GET /api/health` on both listeners. Verified **from shack over the LAN**, not localhost:
  `:5002` answers, `:5001` is connection-refused, both answer on the box itself, `SIGTERM`
  stops it, and the JSON shape is key-for-key and type-for-type identical to the live C#
  host. `rig_connected` is honestly `false` — there is no radio on this VM.
- `./sync.sh` rsyncs the tree to `deck` and builds it there. The VM is the build host on
  purpose: it is where the ALSA and serial work will run.

## The road from here (08/30/2026)

141 routes in the C# `ApiServer.cs` dictionary across 46 groups — captured to
`reference/routes-csharp.txt`, with the live JSON shapes in `reference/contracts.txt`.
Most of those routes are thin CAT wrappers, so this is not 141 hand-written handlers.

### Radio-free — the VM stays on the air. Most of the work.

1. **Simulated rig** ✅ done. `CatTransport` + `SimulatedRig`. The highest-leverage item:
   it turns nearly all 141 routes into radio-free work and gives CI a target it can hit
   forever, since CI will never have a radio.
2. **Status cache + 200 ms poller** ✅ done. `/api/status` served entirely from cache.
3. **Declarative route table** ✅ started. Core operating set done; see below.
4. **Auth/session** ✅ done. See below.
5. **WebSocket** `/ws` and `/ws/tx` — httplib has none, so framing is hand-rolled.
   Testable with synthetic audio.
6. **systemd unit + CI that runs the binary.**

### Needs the radio — the only part where the VM comes down.

7. **Real CAT serial**, `/dev/ttyUSB0` @ 38400, probing only with `ID;`.
8. **ALSA via libasound** rather than `arecord`/`aplay` subprocesses — the actual point of
   the port — plus `/proc/asound` delay, adaptive buffering, PTT tail-wait, watchdog,
   RX mute on TX.

**The cutover gate:** do not book a hardware window until item 7's `SerialCat` is written
and passing against the simulator, so the window is spent *measuring* rather than
debugging parser bugs that a radio-free test would have caught.

## ⚠️ A test that cannot fail is not a test

The obvious staleness check — `SIGSTOP` the process, re-query — is worthless. It freezes
the HTTP server too, so the poller refreshes the cache the instant the process resumes and
the answer comes back fresh. It looks like a pass and measures nothing. Same shape as the
byte-count latency estimate in CARRYOVER.md section 3, which read ~0 in steady state while
435 ms sat in the ALSA buffer: **an estimate whose failure mode is zero looks exactly like
a working measurement.**

The real test (`tests/test_staleness.cpp`, run by ctest) stops the poller and watches the
cache age: `running: 99ms` → `stopped: 2200ms stale=true` against a 1500 ms threshold.

## Auth — done, and verified against the live host (08/30/2026)

The hash format is a **compatibility contract**, not an implementation detail:

```
pbkdf2:<16-byte salt, lowercase hex>:<32-byte hash, lowercase hex>
PBKDF2-HMAC-SHA256, 350000 iterations
```

Change any parameter and every stored credential silently stops working.

**The test that matters is interop, not round-trip.** `tests/test_auth.cpp` verifies a hash
produced by an *independent* PBKDF2 implementation (Python `hashlib`). A round-trip of our
own hasher against our own verifier would pass even if every parameter were wrong — it
proves the code is self-consistent, not that it is compatible. It is in the suite second,
for what little it is worth.

Behaviour confirmed against the VM and then matched:

| | |
|---|---|
| anonymous on the LAN port | **only** `/api/health` and `/api/auth/status` — everything else 401 |
| `allow_anonymous_status` | **off** on the live station (`/api/status` returns 401 on :5002) |
| token transports | cookie `hamdeck_session`, then `Authorization: Bearer`, then `?token=` — in that order |
| login response | token in **Set-Cookie**, never the body; `HttpOnly; SameSite=Strict; Max-Age=28800` |
| throttle | 5 failures → 5 minute lockout |
| failure delay | 500 ms on every rejection, so "no such user" and "wrong password" cost the same |
| 401 body | byte-for-byte identical to the C# host |

**The gate defaults to DENY and runs before routing.** `/api/ptt/on` does not exist yet and
already answers 401 rather than 404, so every route added from here is protected unless
somebody deliberately lists it as anonymous. The opposite default — open unless remembered
— is the shape that leaks, because it fails open every time somebody forgets. Same reasoning
as building a scope lock into a tool instead of trusting the operator to remember it.

**Deliberately NOT implemented:** the C# host transparently upgrades legacy bare-SHA256
hashes to PBKDF2 on successful login. Whether any such hash still exists on the station is
unknown, and inventing a second accepted hash format on a guess widens the auth surface for
no reason. Ask before adding it.

## Route table + command queue + TX watchdog (08/30/2026)

### ⚠️ Request threads NEVER touch the serial port
The serial lock is not re-entrant across threads (CARRYOVER.md section 5), so exactly one
thread — the poller — speaks to the port. Handlers `Enqueue()` a CAT command and return; the
poller drains the queue at the top of each 200 ms cycle and reads state from the cache.
Queueing also gives commands a natural order, which matters for pairs like "set VFO then set
frequency" that are wrong if they interleave.

### The table
Most of the 141 routes are one CAT verb each, so they live in a table rather than 141
hand-written functions: auditable at a glance, and a new route cannot accidentally skip the
auth gate, which has already run. Done: `mode` (6), `vfo`, `split` incl. toggle, `lock`,
`power` (6), `freq`/`freq-b` reads, `test`. Verified by driving the rig through the API and
reading the state back: USB/A/5W → LSB/B/25W/split.

### ⚠️ `/api/ptt/off` is deliberately NOT implemented — it 404s
Unkeying must wait for the audio still queued in the ALSA buffer, or the tail of every
transmission is lost (CARRYOVER.md section 4a), and that wait needs the real device depth
from `/proc/asound`. An unkey that drops PTT immediately would *look* like it works and
quietly cut the end off every over — the exact bug that took a report from a net to find. A
404 is honest; a wrong unkey is not. It lands with the audio work.

### TX watchdog — done, default 180 s
Lives next to the radio, on the poller thread (CARRYOVER.md section 4b). `ptt_timeout_seconds`
matches the C# default of 180; 0 disables. The test asserts the **radio actually stopped**,
read back through `TX;`, not that a trip counter moved — a counter is a claim, `TX0;` is the
outcome.

### ✅ CONFIRMED: the power cap is intentional — 100 W local, 200 W remote

Joe confirmed 08/30/2026. It reads inverted but it is deliberate, so it stays exactly as
ported. **Do not "fix" this.** Not a bug, not a refactor target, not something to raise again.

## How anything actually reaches the host — the network (traced 08/30/2026)

**It is a VPN. WireGuard. Nothing about HamDeck is on the public internet.**

| | |
|---|---|
| server | wg-easy v15 in Docker on **shack** (the VM), `wg0`, udp/51820 published |
| tunnel subnet | `[vpn-net]/24`, server `[vpn-net]` |
| peers | `[vpn-peer-name]` = [vpn-net] (3.6 GiB sent), `iPhone` = [vpn-net] — both enabled, both handshaking, both from endpoint `[vpn-peer]` |
| tunnel mode | **full tunnel** — `AllowedIPs 0.0.0.0/0, ::/0`. The iPhone has none set so it inherits the same default. |
| peer DNS | `[lan-host]` — the LAN router |
| LAN reachability | peers NAT through the container bridge `[container-net]/24` → MASQUERADE → shack `[lan-host]` → LAN. `ip_forward=1`, `wg0` firewall disabled. |
| this site's WAN | `[site-wan]` — **not** the peer endpoint, so those peers are genuinely remote |

### ⚠️ `[reference-host]` is a LAN-ONLY split-DNS name

It resolves **only** on `[lan-host]`. Public resolvers return nothing:

```
@[lan-host] -> [lan-host]        @1.1.1.1 -> (nothing)      @8.8.8.8 -> (nothing)
```

There is no cloudflared ingress for it either. So the WPF client reaches the host **because
it is on the WireGuard tunnel and using the LAN resolver**, not over the internet. This
corrects an earlier note in this file that called it public DNS — it is not.

### What this means for the C++ host

- `[build-host]` should be a **local DNS record on [lan-host]**, exactly like
  `[reference-host]`. No cloudflared ingress, no public exposure, nothing to gate.
- It largely closes the open Authelia question: there is no public surface to put a gate in
  front of. The host's own session auth is the boundary, and the VPN is the perimeter.
- The client's link to the host is WireGuard, full tunnel. That is the "cell link" the
  adaptive buffering in CARRYOVER.md section 3 exists to survive.

## Open decisions (Joe's, not mine)

- **Auth on `[build-host]`.** The host already has its own session cookie. Gating it
  behind Authelia means the panel dies whenever `auth.wa0o.com` is unreachable — a real
  failure mode for a rig operated remotely. Not adding any gate without an explicit yes.
- **Which cloudflared tunnel** publishes it. the VM is not in the shack-pve ingress at all
  today, so there is no existing pattern to copy.

## Next

1. Scaffold CMake + the health route, verify `GET /api/health` over the LAN at
   `[lan-host]`. No hardware needed.
2. CAT layer against `/dev/ttyUSB0` @ 38400, probing **only** with `ID;` → `ID0682;`.
   Never probe with a control route — that changed the operating mode mid-session once.
   Needs a hardware window.
