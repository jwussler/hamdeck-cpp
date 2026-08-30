# The appliance — where this is going

Scoped out 08/30/2026, in conversation, while the C++ port was being built in another session.
**None of this is built.** It is written down so it is not reconstructed worse later.

---

## The goal, in Joe's words

> Flash an SD card, maybe a few router settings, cable it up, and voilà.

A Raspberry Pi behind the rig doing CAT and audio. No VM, no hypervisor, two watts, and it
survives you rebooting everything else. **That is the reason C++ was chosen at all** — see
`CLAUDE.md`.

The audience is **technical but not programmers**: people who can flash a card and follow four
numbered steps, and who cannot recover a bricked Pi with no console.

---

## The experience being aimed at

1. Flash with **Raspberry Pi Imager** — set WiFi and hostname in its customisation dialog
2. Cable it: CAT USB, audio USB, ethernet, power
3. Browse to **`hamdeck.local`**, set a password
4. Point the client at `hamdeck.local`
5. *(only for remote access)* forward a port, or run a tunnel

**Four of those five are things a ham already does.** On a LAN there are no router settings at
all; that is only the remote case.

### Two things that make step 1 and 3 nearly free

- ⚠️ **Pi Imager already solves WiFi onboarding.** Its customisation dialog writes SSID,
  password, hostname and a user before the card is written. Build on Pi OS Lite and honour the
  standard first-boot conventions and **the AP/captive-portal design is not needed for first
  boot** — keep it only as a fallback for when someone changes routers later. That removes a
  chunk of work.
- **mDNS (avahi) gives `hamdeck.local`**, which kills the "what's its IP" problem on a LAN
  entirely and survives DHCP moving the address. The host already has an `/api/auth/setup`
  route: serve a setup page until a password exists, LAN-only, like a router's first-run page.

---

## Hardware

**Raspberry Pi 4, 2–4 GB, wired ethernet.**

- Ethernet is on its own bus. The 3B+ shares ethernet and USB on one USB 2.0 controller, and you
  would be carrying audio *over* USB *to* the network. Don't.
- Four USB ports, so CAT and codec plug straight in. The Zero 2 W has one micro-USB OTG and needs
  a hub, has 512 MB, and is WiFi-only — which adds exactly the jitter the adaptive buffer then
  has to fight.
- ⚠️ **Size the board for TLS, not audio.** Audio is a memcpy at 96 kB/s. The crypto and the
  tunnel are the load.
- 4 GB is not required, but it removes any need to think about tmpfs sizing — see below.

### Read-only root, everything writable in RAM

`/tmp`, `/var/log`, `/var/tmp` on tmpfs; journald `Storage=volatile`; swap off; `noatime`.

**The payoff: if nothing writes to the card, the SD stops being a wear item and you don't need
an SSD at all.** Cheaper, one less USB device (CAT and codec have already taken two), and
power-fail-safe — which matters for a box behind a rig that gets killed at the mains.

Two things must still persist, and they are the trap:

- **Config.** The host writes `config.json` on VFO lock, diversity, adding a user. On a read-only
  root those fail *silently*. Needs a small writable partition.
- **Logs.** Losing them means debugging an overnight fault with nothing to look at — tonight,
  `journalctl` is how the stale status cache was found and how the PTT drain was proven. **Ship
  logs off-box** rather than persisting them locally.

---

## ⚠️ The trap that will bite

**The audio device will not be `hw:CODEC,0` on a Pi.** The Pi has onboard HDMI audio and a
headphone jack, so the USB codec lands as card 1 or 2 — and the index moves with boot order and
whatever else is plugged in.

**Select by name, never by index**: `alsa_capture_device: "hw:CARD=CODEC,DEV=0"`.

This is the same bug that produced `BadDeviceId` on Windows — the client defaulted to device 0
because that is NAudio's default, and index 0 is arbitrary. It was fixed there by storing devices
**by name**. The Pi build needs that discipline from the first commit.

Also: verify `snd-usb-audio` and `cp210x` are present in the Pi image. On the stripped VM kernel
they were **not on disk at all**, and both devices enumerated in `lsusb` while producing no
`/dev/ttyUSB*` and no sound card — which looks exactly like broken passthrough.

---

## Exposure: design for the ham you have

Most operators **will forward a port**. They will not set up a tunnel or a VPN mesh. Designing for
the user you wish you had is how software gets ignored.

So ship UPnP — but **gate it on a strong password**. No default password, none shorter than the
existing 8-character minimum. The host already locks out after 5 failed logins. A forwarded port
with a real password and lockout is fine; with a weak one it is a disaster. Don't lecture; just
don't automate the dangerous half until the safe half exists.

The rest is cheap:

- **Map a non-default port** (8443). Kills essentially all drive-by scanning.
- **Use a UPnP lease** and renew it while the service runs, so the hole closes on its own if the
  Pi dies or is repurposed. Solves the forgotten-forward-from-2019 problem.
- **Only the dashboard listener is exposed.** The control API is already loopback-only.
- **Self-signed cert with fingerprint pinning** in the client — trust on first use, hard-fail on
  change. For a single-user appliance that is *more* secure than a CA cert, and it is what makes
  connecting by IP honest. The failure to avoid is a browser-style "accept and continue" that
  people click through by reflex.
- **Show them their exposure** on the admin page: *"Reachable from the internet at
  203.0.113.4:8443 · 3 failed logins in the last hour."* That does more for real security than
  any documentation, and it fits the project's voice.

⚠️ Remember what makes this different from ordinary self-hosting: **this service keys a
transmitter operating under someone's callsign.** The failure mode is not leaked files.

---

## Updates

Full design in `appliance-updates.md` and `update-prompts.md`. In short: A/B slots via the Pi's
own `tryboot`, a health gate that compares the rig against its *previous* state, never updating
while transmitting or with a client connected, consent scaling with the size of the change, and
signed images. The client tells the operator what to do rather than only that something exists.

---

## What is genuinely unsolved

- **Building the image reproducibly.** `pi-gen` scripts cleanly and can run in CI beside the
  installers. Do it from the first image, not the fifth — same reasoning as putting ARM64 in CI
  from the first binary.
- **One radio, several hosts.** VM 102, VM 105 and eventually the Pi all want the same two USB
  devices. CAT is already solved by the simulator; the contention is only ever the audio codec.
  When the Pi arrives it becomes the permanent host and the VMs become development boxes — which
  makes **simulated audio** as valuable as the CAT simulator already is.

---

## Status

Joe is building the C++ port now, in a separate session. He will say when it is done and this
gets scoped further. **Nothing here should be started unprompted.**
