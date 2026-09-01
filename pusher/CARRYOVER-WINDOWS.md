# Carrying the Wavelog Pusher over to Windows

Written 09/01/2026 on the Linux side, for a session running **on Joe's station PC**, where
the Stream Deck, the Python, and the actual problem all live. Everything below was measured
here unless it says otherwise.

**Read `README.md` beside this file first** — it is the what and why. This is the how, plus
the things that are only true on Windows.

---

## 1. What this is, in one paragraph

A desktop helper for the station PC. It polls the HamDeck host on the rig box, publishes
rig state to Wavelog, stands down while a remote client is operating, and re-exposes the
host's REST API on `localhost:5001` so the station's **44 existing Stream Deck buttons**
work again. Pure Python standard library, Tk window, no third-party runtime dependency.

It replaces `Services/WaveLogServer.cs` from the C# host — `../AUDIT-WAVELOG.md` is the
line-by-line walk of that file and every rule this app is built to.

---

## 2. State of play — what works, what does not

| | status |
|---|---|
| **Wavelog publishing** | ✅ **WORKING IN PRODUCTION.** Confirmed by reading `hamlog.io`'s own `cat` table: `HamDeck / 7192000 / LSB / 100W`, matching the rig. |
| **The window** | ✅ working — radio vs log readouts, in-sync colouring, plain-English state |
| **Handoff to the remote client** | ✅ working, incl. not deferring to its own ghost session |
| **Stream Deck endpoint** | 🔴 **BLOCKED — this is the job.** See §4. |
| **Installer (PyInstaller)** | ⚠️ builds and runs, but **Defender flags it**. See §5. |
| Tray icon | not built; the window minimises to the taskbar |
| Bandmap → QSY | not built, **ask before building** — unauthenticated VFO control |

---

## 3. Getting it running on the PC

Two ways in. **Prefer the source zip** — it is what sidesteps Defender.

### a. From source (recommended)
`HamDeckPusher-source.zip` on
<https://github.com/jwussler/hamdeck-cpp/releases/latest> — 33 KB, no packer.
Unzip anywhere. Python 3.12+ is already on this PC at
`%LOCALAPPDATA%\Programs\Python\Python313` (installed for the NetLogger→Wavelog sync).

| script | use it for |
|---|---|
| `Run HamDeck Pusher.cmd` | normal launch, no console |
| `Run with console (shows errors).cmd` | **when something is wrong** — a windowed build has nowhere to print a traceback |
| `Diagnose.cmd` | `--status`, `--selftest`, then one real pass |

### b. From the repo
    git clone https://github.com/jwussler/hamdeck-cpp.git     # PRIVATE - needs auth
    cd hamdeck-cpp\pusher
    python -m hamdeck_pusher --status      # settings, secrets hidden
    python -m hamdeck_pusher --selftest    # offline proof of the decision path + window
    python -m hamdeck_pusher --once        # one real pass, prints what it decided
    python -m hamdeck_pusher --gui         # the window

### Settings
`%APPDATA%\HamDeckPusher\settings.json`, mode 0600, **never touched by an installer**.

| field | value on this station |
|---|---|
| `host_url` | `http://192.168.40.64:5002` (the rig box, "deck") |
| `host_user` | `wa0o` |
| `host_password` | Joe has it. Also on the Linux box at `/tmp/password_hamdeck.txt` (0600). ⚠️ That file is **CRLF** — the username line carries a trailing `\r` that must be stripped or the login fails with a username that looks perfectly correct. |
| `wavelog_url` | `https://hamlog.io` — **no `/index.php`**; that is only the test instance |
| `wavelog_key` | Joe created it in Wavelog → Account → API Keys |
| `radio_name` | currently `HamDeck`. ⚠️ This is **Wavelog's key for the radio row** — two publishers sharing a name overwrite each other |
| `deck_port` | `5001` to enable, `0` to disable |

---

## 4. 🔴 THE OPEN PROBLEM — the Stream Deck endpoint will not bind

Observed on the PC:

    Stream Deck endpoint OFF - port 5001:
    An attempt was made to access a socket in a way forbidden by its access permissions

That is Winsock **WSAEACCES (10013)**.

⚠️ **Windows words it like a permissions failure and it almost never is one.** It is not
the firewall and not Defender. Hyper-V, WSL and Docker Desktop reserve large blocks of TCP
ports, and **nothing can bind inside one**. The port is not in use — it is spoken for.

### Diagnose (Administrator)
    netsh interface ipv4 show excludedportrange protocol=tcp

Look for a range containing **5001**.

### Fix, if it is reserved (Administrator)
    net stop winnat
    netsh int ipv4 add excludedportrange protocol=tcp startport=5001 numberofports=1 store=persistent
    net start winnat

⚠️ **Do NOT solve this by choosing another port.** All 44 Stream Deck buttons target
`localhost:5001` (C# `AUDIT.md` §11). Reserving the port is far less work than editing 44
buttons, and the buttons are the thing this feature exists to preserve.

### If it is NOT in an excluded range
Then find who holds it, and say so rather than guessing:

    netstat -ano | findstr :5001
    Get-Process -Id <pid>

An older copy of this app is a very common answer.

### Proving it once it binds
    curl http://127.0.0.1:5001/api/status
    curl http://localhost:5001/api/status
    curl "http://[::1]:5001/api/status"

⚠️ **All three must answer.** `localhost` resolves to `::1` FIRST on Windows, and a client
that does not fall back to IPv4 — the Stream Deck plugin is one — gets connection refused
from a listener running perfectly well on 127.0.0.1. The app binds both families for
exactly this reason; the C# used `HttpListener`, whose `localhost` prefix covered both, so
it never surfaced there.

---

## 5. ⚠️ Defender flags the installer — false positive, and what to do

Unsigned **PyInstaller** builds get flagged by Defender's ML heuristics (`Wacatac`,
`Wacapew`). The heuristic keys on the **packer**, not on anything in the code.

- **Short answer: use the source zip.** No packer, nothing to flag.
- Code signing is the only real cure. SignPath Foundation is free **but requires a public
  repo**, and `hamdeck-cpp` is private — see `[[hamdeck-code-signing]]`.
- If Defender **quarantined** the exe, the app never ran at all, which looks identical to
  a bug in the app. Check Windows Security → Protection history before chasing anything.

### The firewall prompt is a separate thing, and the answer is Cancel
Windows Defender Firewall prompts the first time anything listens on a socket.
**Loopback traffic is not filtered by the firewall at all** — denying it leaves every
button working, and allowing it opens the app to the network for no benefit. If Allow was
already clicked, the rule is worth removing.

---

## 6. The Stream Deck, precisely

⚠️ **There is no plugin to write and never was.** The C# repo's entire implementation is
one README line: *"install the API Ninja plugin and point buttons at
`http://localhost:5001/api/{endpoint}` with GET."* What exists is **44 configured buttons**.
The C# host ran **on this PC**, so `localhost:5001` was the host. The rig moved to its own
box and the URL became nothing. This app makes it answer again by holding a session — a
button cannot log in.

⚠️ **The loopback bind is the entire security model.** Anything that can reach that port
drives the radio with no password. That is the deal the C# made deliberately (`AUDIT.md`
§11). So it binds `127.0.0.1` and `::1` explicitly, never `0.0.0.0` or `::`.

**Measured against a simulator, never the live rig: 71 of 74 button routes answer 200.**
The three that do not, and why:

| route | why |
|---|---|
| `/api/cw/send/*`, `/api/cw/memory/*` | CW keyer not ported (`Services/Keyers.cs`, 127 lines) |
| `/api/voice/play/*` | voice keyer not ported |
| `/api/rxant/*` | kmtronic hardware not on this host |

⚠️ **`/api/att/toggle` works.** A route-inventory diff flagged it as missing because the
on/off/toggle variants are generated at runtime rather than written as literals — the
repo's own *"inventories are not behaviour"* rule, arriving as a false alarm.

---

## 7. Accounts, and one trap worth more than the rest

On the host: `joe` (admin+tx) · `listener` (no tx) · `pusher` (dormant, a test account) ·
**`wa0o`** — what the app uses, **granted transmit** so the deck's PTT buttons work.

⚠️ **NEVER log the remote client in as `wa0o`.** The pusher recognises its own leftover
sessions by *username* so it can tell its ghost from a real operator. An operator on that
same account gets subtracted as a ghost, and the pusher will not stand down — two things
writing to the log while you operate.

⚠️ **`can_transmit` did not gate PTT until 09/01/2026.** The C++ host gated only `/ws/tx`,
so a receive-only account could still key the rig, send CW and play voice memories. Now
ported from `ApiServer.cs` ~789, guarded by `test_transmit_gate`.

---

## 8. Rules this app was built to — do not relearn them

Every one is a defect found in the C# it replaces. `../AUDIT-WAVELOG.md` has the detail.

- **Never publish a stale reading.** `/api/status` carries `stale` and `cache_age_ms`. A
  missing field counts as stale, never fresh.
- **Never fail silently.** The C# logged only on success, at debug. A wrong API key
  produced no output anywhere.
- **`record_published` only on success.** Recording an attempt resets the heartbeat and
  the change test, so a rejected post is never retried while the app believes it is fine.
- **The settle window is tracked while deferring**, so the handoff publishes instantly when
  a client closes — that is when the log is most stale.
- **Power and TX never trigger a post.**
- ⚠️ **`allow_reuse_address` must stay OFF on Windows.** On Linux it only shortens
  TIME_WAIT; on Windows it lets a **second copy bind 5001 with no error**, and requests go
  to whichever socket wins. Two copies fighting over 44 buttons, silently.
- ⚠️ **A gate is not a gate until it FAILS on demand.** Every gate here was proven by
  reintroducing its bug. Two of the first six did not fail and therefore were not gates.
- ⚠️ **Never probe a live rig with a control route.** Reading is safe. To test a transmit
  gate use `/api/ptt/OFF` — if the gate is broken that unkeys a rig that is not keyed,
  where `/on` puts the station on the air to prove a point.
- ⚠️ **Set a User-Agent** on Wavelog calls. Cloudflare has 403'd (error 1010) a client
  that sends none.

---

## 9. Tests and shipping

    cd pusher
    python -m unittest discover -s tests -v      # 37 tests
    python -m hamdeck_pusher --selftest          # decision path + a REAL Tk window

CI (`.github/workflows/build.yml`) runs both on every push. The release workflow
(`release.yml`, job `pusher-windows`) runs them on Windows, freezes with PyInstaller, runs
`--selftest` **on the frozen exe**, checks the icon is really inside it, then builds the
Inno installer.

**To ship: push a tag.** `git tag v0.1.19 && git push origin v0.1.19` builds everything and
publishes a GitHub Release with the installers attached. ⚠️ Host changes are a different
path entirely — `tools/deploy.sh` on the rig box, never a tag.

⚠️ **A green ctest does not mean a green build.** They are separate targets; a host binary
that failed to compile has been observed alongside "100% tests passed".

---

## 10. Where everything is

| | |
|---|---|
| this app | `pusher/` in `jwussler/hamdeck-cpp` (**private**) |
| why it is built this way | `pusher/README.md`, `AUDIT-WAVELOG.md` |
| the C++ host | same repo, `src/`; runs on **deck**, `192.168.40.64`, service `hamdeck-cpp` |
| the C# reference | `jwussler/HamDeck` — `Services/WaveLogServer.cs`, `AUDIT.md` §11, `README.md` line 100 |
| build log / open list | `WIP.md` §11, and §10 for the host's own open items |
| host deploy | `HAMDECK_BUILD_HOST=deck ./tools/deploy.sh` (⚠️ `sync.sh` installs nothing) |

---

## 11. What to do first, in order

1. **`netsh interface ipv4 show excludedportrange protocol=tcp`** — is 5001 reserved?
2. If yes, reserve it back (§4) and restart the app.
3. Confirm all three URLs answer (§4), then press a real Stream Deck button.
4. Report which buttons work. Expect CW, voice memories and RX antenna to fail — those are
   known unported features, not new bugs.
5. If Defender quarantined anything, say so before anything else is diagnosed.
