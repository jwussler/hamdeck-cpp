# HamDeck Data Pusher

A desktop helper for the station PC. Three jobs, one process:

1. **Push live rig state to Wavelog** (`POST /api/radio`) so the log follows the radio.
2. **Stand down when a remote client is operating**, and say so out loud rather than
   going quiet.
3. **Hold the host session** so a Stream Deck can fire plain unauthenticated GETs at
   `127.0.0.1` and have them reach the rig.

## Why job 3 exists (measured, 09/01/2026)

From the station PC, against the C++ host on the rig box:

    GET :5002/api/status     -> 401
    GET :5002/api/agc/fast   -> 401 {"message":"Authentication required"}
    GET :5001/api/status     -> unreachable (control listener is loopback-only)

So a Stream Deck HTTP plugin **cannot drive the rig at all** on its own. It has no way to
log in and no way to keep a session alive. This helper does both and re-exposes the routes
locally, which is also the only place a no-auth endpoint is defensible.

## The rules it is built to, all of them earned

Read `../AUDIT-WAVELOG.md` first — it is the walk-down of the C# `WaveLogServer.cs` this
replaces, and every rule below is a defect found in it.

- ⚠️ **Never publish a stale reading.** The host reports `stale` and `cache_age_ms` on
  `/api/status`. The C# had no such signal and would happily republish a frozen frequency
  forever if its poll died. **A stale value looks exactly like a current one.**
- ⚠️ **Never fail silently.** The C# logged only on SUCCESS, at debug level. A wrong API
  key produced no output anywhere. Here the last result is first-class state, and a
  failing push is visible without reading a log.
- ⚠️ **Exactly one publisher.** Wavelog keys its radio row by the `radio` NAME, so two
  pushers on one name race and the log shows whichever POST landed last. The name is
  configurable for that reason - it is not decoration.
- ⚠️ **Deferring is not the same as being broken, and both are visible.** When a remote
  client is operating this stops publishing on purpose. That state is reported, because
  "nobody is publishing" and "publishing is failing" look identical from Wavelog.
- ⚠️ **Set a User-Agent.** Cloudflare in front of hamlog.io answers 403 (error 1010) to
  any client that sends none.

## Setup

The host account exists already as **`pusher`** (no admin, **no transmit** — it never needs
to key anything, and an account that cannot transmit is one fewer credential that can). Set
a password you know:

    ssh deck
    HAMDECK_CONFIG=/etc/hamdeck-cpp/config.json \
      python3 ~/hamdeck-cpp/tools/set_password.py pusher
    sudo systemctl restart hamdeck-cpp

Then write `%APPDATA%\HamDeckPusher\settings.json` (0600; it holds two secrets):

    {
      "host_url":     "http://192.168.40.64:5002",
      "host_user":    "pusher",
      "host_password": "...",
      "wavelog_url":  "https://hamlog.io",
      "wavelog_key":  "...",
      "radio_name":   "FTDX-101MP"
    }

⚠️ `radio_name` is Wavelog's key for the radio row — name it for the *station*, not the
software, and never share one name between two publishers.

⚠️ On the test instance the API base includes `index.php`
(`http://127.0.0.1:8081/index.php`); the public site rewrites it away. Check which your
target needs — a 404 here looks like a bad key.

    python3 -m hamdeck_pusher --selftest    # offline proof of the decision path
    python3 -m hamdeck_pusher --status      # settings, redacted
    python3 -m hamdeck_pusher --once        # one pass, prints what it decided
    python3 -m hamdeck_pusher              # run

## Proven, on the live station 09/01/2026

- publishes real rig state and the Wavelog row **changes** (read back from the database,
  not inferred from a 200)
- refuses a stale reading, a disconnected rig, and an unreachable host, each with its own
  reason
- stands down while a remote client operates, and takes the station back the instant it
  closes — with **no extra settle delay**, because the log is most out of date exactly then
- reports a rejected key as a failure and **retries** it, rather than counting it published
- ⚠️ does **not** defer to its own ghost — see below

### The bug this found in itself
Run it, let it exit, run it again inside 15 seconds and the first version reported
*"a remote client is operating the station"* — against nothing but its own previous
session. Token exclusion cannot catch that: the old session genuinely is a different one.
Under a crash-restart loop it would have stood down **forever**, silently, because
deferring and working look identical from Wavelog. Fixed at both ends: the host reports
`same_user_clients` so a caller can subtract its own, and the pusher logs out on exit.

## The window

It answers **one** question: *is my log following my radio?* So it shows both readouts side
by side — RADIO and IN THE LOG — with one line of plain English underneath. The log readout
lights amber when it matches the radio and dims when it does not, so "the log is behind" is
visible without reading a word.

Three states that must never look alike, and do not:

| on screen | means |
|---|---|
| **LOGGING** (green) | Wavelog is current |
| **STANDING BY** (amber) | a remote client is operating; not publishing **on purpose** |
| **NOT LOGGING** (red) | it tried and failed — with the server's own words |

Brand tokens are taken verbatim from `~/hamdeck-site/brand/BRAND.md`. Tk only: no
third-party GUI dependency, so the licence surface stays clean for code signing.

## ⚠️ Windows Defender flags the installer — it is a false positive

**Measured 09/01/2026 on the station PC:** `Trojan:Win32/Bearfoos.A!ml`, three detections,
**all on `HamDeckPusher-0.1.0-setup.exe`** and **zero on `hamdeck-pusher.exe`**. So the
heuristic is keying on **the unsigned Inno installer**, not on the PyInstaller freeze — the
usual packer mitigations (`--onedir`, no UPX, real icon) were already in place and did not
help. The `!ml` suffix means a machine-learning verdict rather than a signature match.

### Making it clean for someone who is not the author — two sequenced steps

**1. File the false-positive report (free, ~1-3 days, clears it for everyone).**
<https://www.microsoft.com/en-us/wdsi/filesubmission>, submitting as *software developer*.
⚠️ **It is per file hash, so every release must be resubmitted** — which is exactly why it
is a stopgap and not the answer.

**2. Sign with Azure Artifact Signing (~$9.99/mo) — the real fix.**
Eligibility confirmed: individuals supported, **USA + Canada only**. **No hardware token**,
so it signs from a GitHub-hosted runner — which matters because since June 2023 an OV key
must live on an HSM or USB token, and a token cannot sign from CI at all.

`release.yml` is **already wired for it** and skips cleanly until the secrets exist. Add
these repository secrets and the next tag ships signed, no code change:

    AZURE_TENANT_ID  AZURE_CLIENT_ID  AZURE_CLIENT_SECRET
    AZURE_SIGNING_ENDPOINT  AZURE_SIGNING_ACCOUNT  AZURE_SIGNING_PROFILE

⚠️ **Both the frozen exe and the installer get signed**, in that order — signing only the
freeze would leave the detection above untouched.
⚠️ **Signing buys no instant SmartScreen trust.** Reputation accrues per publisher over
downloads and time; EV certificates stopped bypassing SmartScreen in 2024, so the EV
premium buys nothing. Early downloads still warn. That is normal, not a broken certificate.

## Installing on Windows

The installer is built by CI and attached to the GitHub Release for each tag — the repo is
private, so the release is too.

    https://github.com/jwussler/hamdeck-cpp/releases/latest

⚠️ **Unsigned.** SmartScreen will warn on first run. Saying so is the honest position.

Settings live in `%APPDATA%\HamDeckPusher\settings.json` and the installer **never touches
them**, so an update cannot overwrite the API key. "Start when I sign in" is an unchecked
task, not a default — something that writes to a logbook on every boot is the operator's
decision.

## The Stream Deck endpoint

⚠️ **It exists so EXISTING buttons keep working, not so new ones can be made.**

From the C# host's `AUDIT.md` §11: *"Every Stream Deck button on this station targets
`localhost:5001`; all 44 of them, with no LAN address anywhere."* That worked because the
C# host **ran on the station PC**. The rig moved to its own box and that URL became
nothing — which is why the deck went dead, and why the fix is to make the same URL answer
again rather than to edit 44 buttons.

Set `deck_port` to **5001** in Settings. Off by default (`0`), because a no-auth endpoint
must be opted into.

⚠️ **The loopback bind IS the security model, and nothing else.** Anything that can reach
that port drives the radio with no password — exactly the deal the C# host made, on
purpose, for the same reason. So it binds `127.0.0.1` explicitly (a kernel guarantee, not
a check somebody can forget) *and* refuses a non-loopback peer if it ever finds one. Both
halves are asserted in `tests/test_deck.py`.

It shares the pusher's session, so one login serves both and one re-login fixes both.

⚠️ **It listens on BOTH `127.0.0.1` and `[::1]`, and that is not belt-and-braces.** The
buttons say `http://localhost:5001/…`, and on Windows **`localhost` resolves to `::1`
first**. A client that does not fall back to IPv4 gets connection refused from a listener
running perfectly well on 127.0.0.1 — the app reports the endpoint as up and every button
fails. The C# used `HttpListener`, whose `localhost` prefix covers both families, so this
never surfaced there. `::1` is as local as `127.0.0.1`, so the security model is unchanged.

### ⚠️ "An attempt was made to access a socket in a way forbidden by its access permissions"

Winsock **WSAEACCES (10013)** on bind. Windows words it like a permissions failure; it is
not one, and it is not the firewall or Defender either.

🔴 **The first answer written here was wrong, and its prescribed fix was harmful.** It
blamed a Hyper-V/WSL/Docker port reservation and told the operator to run
`netsh int ipv4 add excludedportrange ... store=persistent`. Measured on the station PC
09/01/2026, the real holder was **the legacy C# HamDeck host still running on that PC**,
holding `HTTP://+:5001/` through http.sys. That "fix" would have created an *administered*
exclusion, after which **nothing could bind 5001 again — this app included — across
reboots.** A transient conflict made permanent. Never run it.

⚠️ **The trap that made the wrong answer look confirmed:** an http.sys registration also
appears in `show excludedportrange`. **Only rows marked `*` are administered reservations.**
`5357` (WSDAPI) sits right beside it for the same reason.

Find the actual holder:

    netstat -ano | findstr :5001          # owner PID 4 = http.sys, i.e. a .NET host
    netsh http show servicestate view=requestq

If it is the old C# host, stop it — **and remove `HamDeck` from `shell:startup`**, or it
takes the port back at the next logon and the deck dies again with the same message.

⚠️ **Do not solve this by choosing another port.** 44 Stream Deck buttons point at 5001.

⚠️ **Windows will show a firewall prompt the first time. Click Cancel.** Loopback traffic
is not filtered by Windows Firewall at all, so denying it leaves every button working,
while allowing it opens the app to the network for no benefit whatsoever. The window says
this too, where you are actually looking when the dialog appears.

⚠️ **Windows lets a second process bind a port that is already in use.** `http.server`
sets `allow_reuse_address`, which on Linux only shortens TIME_WAIT and still refuses a
live second listener — on Windows it means *share it*, so a second copy of this app would
bind 5001 with **no error** and requests would go to whichever socket won. Two copies
fighting over 44 buttons, silently. Off on Windows, on elsewhere. Caught by CI failing on
Windows while passing on Linux, which is the only reason it was found.

### What actually works — measured against a simulator, never your rig

74 button routes from the C# README's table, fired at a **simulated** host (the walker
refuses any target that does not answer `simulated: true`, and has no `--force`):

| result | count |
|---|---|
| answered 200 | 71 |
| answered but say they cannot | 3 |

The three that will not work, and why — all known unported features, not surprises:

- **CW keyer** (`/api/cw/send/*`, `/api/cw/memory/*`) — *"not implemented in the C++ host
  yet"*. `Services/Keyers.cs`, 127 lines, still on the §10 list.
- **Voice memories** (`/api/voice/play/*`) — same.
- **RX antenna** (`/api/rxant/*`) — kmtronic hardware not on this host.

⚠️ **`/api/att/toggle` DOES work.** A route-inventory diff flagged it as missing because
the on/off/toggle variants are generated at runtime by string concatenation rather than
written as literals. That is the repo's own warning — *comparing route inventories is not
comparing behaviour* — landing in the opposite direction, as a false alarm.

### ⚠️ Transmit rights are a separate thing from having an account
`can_transmit=false` blocks `/api/ptt/*`, `/api/cw/*` and `/api/voice/play/*` with a 403,
so PTT buttons are dead without it. That gate did not exist on this host until 09/01/2026 —
only `/ws/tx` was gated, so an account marked receive-only could still key the rig. Ported
from `ApiServer.cs` ~789 and guarded by `test_transmit_gate`.

## Still to build

- **A tray icon.** The window minimises to the taskbar today. A real tray icon needs
  `pystray` (LGPL) or Win32 `Shell_NotifyIcon` via ctypes; the first dirties the licence
  surface for SignPath, so it was not taken on a whim.
- **Mode strings are passed through as the rig reports them.** Wavelog normalised `CW-R`
  to `CW` on its own during the live run. Worth checking the full set the FTDX-101 emits
  before trusting every one of them.
- **Bandmap → QSY** (the C#'s HTTP :54321). Not built, and **ask first**: it is
  unauthenticated remote control of the VFO, and the C# bound it to the whole LAN.
