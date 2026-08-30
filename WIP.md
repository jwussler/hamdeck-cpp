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
window**. That is a deliberate act with the station off the air, not something to slide
into:

```
ssh <reference-host> 'sudo systemctl stop the host service'
ssh <hypervisor> 'qm <op> <vmid>; qm <op> <vmid> -delete usb0 -delete usb1
         qm <op> <vmid> -usb0 host=10c4:ea70 -usb1 host=08bb:29c3; qm <op> <vmid>'
# ... and the exact reverse to give the radio back ...
```

Do not leave the station in that state unattended. Reverse it at the end of every session.

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
