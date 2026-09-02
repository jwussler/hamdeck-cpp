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
| **Stream Deck endpoint** | ✅ **WORKING 09/01/2026** — bound on 5001 (v4+v6), all three loopback URLs answer, the deck plugin is polling it. See §4. |
| **Installer** | ⚠️ works, but Defender flags the **Inno setup exe** (not the PyInstaller build). Installs and runs anyway. See §5. |
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

## 4. ✅ SOLVED 09/01/2026 — and the cause was NOT a port reservation

Observed on the PC:

    Stream Deck endpoint OFF - port 5001:
    An attempt was made to access a socket in a way forbidden by its access permissions

That is Winsock **WSAEACCES (10013)**.

🔴 **The Hyper-V / WSL / Docker guess in the original draft of this section was WRONG.**
It was written from the Linux side without measuring, and it was plausible enough to survive
review. The real holder was **the old C# HamDeck host, still running on this very PC.**

### What it actually was — measured on the station PC, 09/01/2026
- `netstat -ano | findstr :5001` → `0.0.0.0:5001` and `[::]:5001` LISTENING, owner **PID 4**.
  PID 4 is `System`, i.e. the **http.sys** kernel driver — which is exactly what a .NET
  `HttpListener` binds through. That is a .NET host's signature, not a NAT reservation.
- `netsh http show servicestate view=requestq` → **`HTTP://+:5001/`** and **`HTTP://+:5002/`**
  registered. Those are precisely the C# host's two documented listeners.
- `Get-Process` → **`HamDeck.exe` PID 37868**, `C:\Program Files\HamDeck\HamDeck.exe`,
  auto-started at logon from `shell:startup`.
- `curl http://127.0.0.1:5001/api/health` →
  `{"status":"ok","service":"HamDeck API (C#)","version":"3.4.14","rig_connected":false}`
- **`winnat` was `Stopped`.** No `LxssManager`, no Docker service installed. There was nothing
  running behind the Hyper-V theory at all.

### ⚠️ THE TRAP THAT MADE THE WRONG ANSWER LOOK RIGHT
`netsh interface ipv4 show excludedportrange protocol=tcp` **did list 5001** — and 5002:

    Start Port    End Port
    ----------    --------
          5001        5001
          5002        5002
          5357        5357
         50000       50059     *
    * - Administered port exclusions.

So step 1 of §11 answered **"yes, 5001 is reserved"** — and following that answer to its
prescribed fix would have been a disaster. **An http.sys registration appears in this list
too.** Note `5357` sitting right beside it: that is WSDAPI, also http.sys, also not Hyper-V.

**The tell is the `*`.** Only `50000-50059` is an *administered* (deliberately reserved)
range. Non-administered single-port rows that happen to match your own application's
listeners are your application.

**Proof, not inference:** stopping `HamDeck.exe` made the **5001 and 5002 rows vanish from
`show excludedportrange` immediately**. A winnat/Hyper-V reservation does not behave that way.

### 🔴 DO NOT RUN THE OLD "FIX" — it would have made this permanent
    net stop winnat                                            # winnat was not even running
    netsh int ipv4 add excludedportrange protocol=tcp \
          startport=5001 numberofports=1 store=persistent      # <-- HARMFUL

That creates an **administered** exclusion for 5001, after which **nothing can bind it —
including this pusher**. It converts a transient process conflict into a permanent one, and
`store=persistent` means it survives reboots. It was never run here. Do not run it.

### The actual fix
Stop the legacy C# host. Checks done first, so this is safe rather than hopeful:
`rig_connected:false` (the rig lives on **deck**, `192.168.40.64` now), and the local
cloudflared tunnel that used to publish its `:5002` as the station hostname **is not running on
this PC** — so nothing public depends on it.

    Stop-Process -Name HamDeck -Force
    Stop-Process -Name hamdeck-pusher -Force        # then relaunch it, so it retries the bind
    Start-Process "C:\Program Files\HamDeckPusher\hamdeck-pusher.exe"

🔴 **`HamDeck.lnk` is STILL in `shell:startup`.** The C# host comes back at the next logon
and takes 5001 again, and the deck dies again with the same misleading error. Removing that
shortcut is what makes this fix outlive one session — see §11.

### Proving it once it binds
    curl http://127.0.0.1:5001/api/status
    curl http://localhost:5001/api/status
    curl "http://[::1]:5001/api/status"

⚠️ **All three must answer.** `localhost` resolves to `::1` FIRST on Windows, and a client
that does not fall back to IPv4 — the Stream Deck plugin is one — gets connection refused
from a listener running perfectly well on 127.0.0.1. The app binds both families for
exactly this reason; the C# used `HttpListener`, whose `localhost` prefix covered both, so
it never surfaced there.

**✅ Confirmed 09/01/2026 — all three returned HTTP 200 from the live rig:**
`{"connected":true,"freq":7192000,"mode":"LSB","vfo":"A","power":100,"tx":false,...}`
Both families bound by one pusher PID: `127.0.0.1:5001` + `[::1]:5001`.

### ⚠️ One belief this disproved
The BarRaider **API Ninja** plugin (`com.barraider.apininja.exe`, PID 34676) had been
polling `localhost:5001` the whole time — and **getting HTTP 200s from the stale C# host.**
So "the rig moved and that URL became nothing, which is why the deck went dead" is *not*
what happened. The URL answered fine; it was a host with no radio behind it.

**A 200 on `/api/status` is not proof you reached the right host.** Check `service` and
`rig_connected` in `/api/health` before concluding anything about who is listening.

---

## 5. ⚠️ Defender flags the installer — measured 09/01/2026

**It is a false positive, and the app ran anyway. Both halves of that matter.**

### What Defender actually did — read from the machine, not assumed
    Get-MpThreatDetection | Select InitialDetectionTime, ThreatID, Resources
    Get-MpThreat          | Select ThreatName, SeverityID, IsActive

- **`Trojan:Win32/Bearfoos.A!ml`**, severity 5, `IsActive: False` (remediated).
  The `!ml` suffix is the giveaway: a machine-learning heuristic verdict, not a signature match.
  ⚠️ The earlier guess in this doc was `Wacatac`/`Wacapew` — same family of ML heuristics,
  **wrong name**. Read the real one; the name is how you file a false-positive report.
- **Three detections, 08/31/2026 19:47 / 20:58 / 21:09 — all on
  `HamDeckPusher-0.1.0-setup.exe`**, in `D:\Data\Personal\Downloads` and a WinRAR temp dir.
- 🔴 **ZERO detections on `hamdeck-pusher.exe`** — the PyInstaller output itself was never
  touched.

### ⚠️ So the flag is on the INNO SETUP INSTALLER, not on the PyInstaller build
That reverses this section's original advice. The usual PyInstaller mitigations were
**already in place and did not help**: `.github/workflows/release.yml` builds `--onedir`
(not `--onefile`, so there is no self-extracting stub), uses **no UPX**, and embeds a real
icon. The heuristic is keying on *an unsigned Inno installer that drops an unsigned
freeze* — not on the packer this section blamed.

### 🔴 "Quarantined ⇒ the app never ran" is NOT a safe inference
When this session started, **`hamdeck-pusher.exe` was running** (PID 48848, started
08/31 20:58:34 — the same second as the second detection). The install completed and
launched; Defender then removed the leftover *setup* file. The app had been running for
hours while the working theory was that it had never started.

**Check `Get-Process`, not just Protection history.** They answer different questions.

### Making it clean for someone who is not Joe — the options, ranked
The source zip sidesteps this for us, but it is not an answer for another operator.

| option | cost | reality |
|---|---|---|
| **Report the false positive to Microsoft** | free | <https://www.microsoft.com/en-us/wdsi/filesubmission>, as *software developer*. Reclassified in ~1-3 days and it clears for **everyone**. ⚠️ **Per file hash — every new release must be resubmitted.** The realistic near-term fix. |
| **Azure Artifact Signing** | ~$9.99/mo | ✅ **THE LONG-TERM ANSWER — eligibility CONFIRMED 09/01/2026.** Renamed from *Azure Trusted Signing*; now GA. Individuals **are** supported, **USA + Canada only** — Joe qualifies. **No hardware token**, signs from CI. |
| SignPath Foundation | free | 🔴 **BLOCKED for this repo**: it requires a *public* repo under an OSI-approved licence. `hamdeck-cpp` is **private with no licence file**. (The C# `HamDeck` repo is public/MIT — different repo, different answer.) |
| `--version-file` metadata | free | Adds company/product/version resources to the frozen exe. Lowers the heuristic score a little. Not currently passed; cheap to add, do not expect it to be sufficient alone. |
| Source zip | free | Already the recommendation, and it genuinely never flags — but it needs Python on the target PC. |

### ⚠️ Why a USB-token OV cert is the wrong shape for THIS project
Since June 2023 the CA/Browser Forum requires an OV private key to live on an HSM or USB
token. **A USB token cannot sign from a GitHub-hosted `windows-latest` runner**, and that is
exactly where `release.yml` builds the installer. Azure Artifact Signing signs over a cloud
API with no token, which is the only reason it fits the existing CI without redesigning it.

⚠️ **Signing does NOT silence SmartScreen immediately** — reputation accrues per publisher
identity over downloads and time, and Microsoft's own docs say AAS gives no instant trust.
**EV certificates stopped bypassing SmartScreen in 2024**, so the EV premium buys nothing
here. Early downloads still warn. That is normal, not a broken cert.

**These two are sequenced, not either/or:** file the free false-positive report to clear the
build that exists today; adopt AAS so each release inherits the last one's reputation instead
of restarting from zero. See `[[hamdeck-code-signing]]`.

⚠️ **Sign BOTH the frozen exe and the Inno installer.** Defender is flagging the installer,
so signing only the PyInstaller output would leave the actual detection untouched. Inno has a
`SignTool` directive for this.

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

**Updated 09/01/2026 — steps 1-3 of the original list are DONE, and step 1 was actively
misleading. Do not re-run it as written; read §4 first.**

### Done on the station PC 09/01/2026
1. ✅ Defender history read (§5) — `Bearfoos.A!ml` on the **setup exe** three times; the
   PyInstaller exe was never flagged, and **the app was running the whole time**.
2. ✅ Cause of the bind failure found (§4) — **the legacy C# host `HamDeck.exe` held
   `HTTP://+:5001/`**, not a Hyper-V/WSL reservation. `winnat` was stopped.
3. ✅ Legacy host stopped; pusher restarted; **all three loopback URLs answer 200** with
   live rig state, on both `127.0.0.1` and `[::1]`.

### Next, in order
1. 🔴 **Remove `HamDeck.lnk` from `shell:startup`** (or the C# host retakes 5001 at the next
   logon and the deck dies again with the same misleading error). Joe's call, because it also
   ends the local the station hostname origin — which is already dead here anyway, since
   `cloudflared` is not running on this PC.
2. **Press real Stream Deck buttons and report which work.** Expect **CW keyer**, **voice
   memories** and **RX antenna** to fail — known unported features (§6), not new bugs.
3. **File the Microsoft false-positive report** for the installer (§5) so the next person to
   install it does not get a scary red box. Free, ~1-3 days, must be redone per release.
4. Decide the signing path — Azure Trusted Signing is the only unblocked real cure while
   `hamdeck-cpp` stays private (§5).

### ⚠️ The lesson worth carrying back to the Linux side
This section originally opened with *"`netsh ... show excludedportrange` — is 5001
reserved?"* It **was** listed, so the answer was "yes" — and the prescribed follow-up would
have created an administered reservation that permanently blocked the pusher from its own
port. **The diagnostic was right, its interpretation was wrong, and a confident doc written
from measurements taken on the *other* machine is exactly how that happens.**
Same rule as `[[prove-the-gate-fails]]`: a check that cannot distinguish two causes is not
a check. Match the *owner*, not the presence of a row.
