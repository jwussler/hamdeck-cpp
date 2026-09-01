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

## Still to build

- **The Stream Deck endpoint** (`deck_port`). Design is settled — a loopback-only listener
  that maps short button paths onto host routes using the session this already holds — but
  it is not written. `deck_port: 0` disables it, and that is the default because a no-auth
  endpoint must be opted into, never defaulted on.
- Tray icon + settings GUI, PyInstaller bundle, Inno installer. The
  `netlogger-wavelog-sync` build (`build.ps1`: tests → freeze → `--selftest` on the frozen
  exe → installer) is the pattern to copy; `--selftest` exists here for exactly that step.
- **Mode strings are passed through as the rig reports them.** Wavelog normalised `CW-R`
  to `CW` on its own during the live run. Worth checking the full set the FTDX-101 emits
  before trusting every one of them.
