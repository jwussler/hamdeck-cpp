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
3. **Declarative route table** — build the table, not 141 functions.
4. **Auth/session.** Only `/api/health` and `/api/auth/status` are ever anonymous
   (`AlwaysAnonymousRoutes` in `ApiServer.cs`); `allow_anonymous_status` gates the
   read-only set. Cookie, `?token=`, `Bearer`.
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
