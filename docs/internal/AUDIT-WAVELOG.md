# Wavelog + Stream Deck — walking the C# down before writing anything

Reference: `jwussler/HamDeck`, `Services/WaveLogServer.cs` (289 lines), read line by line
08/31/2026. Local clone: `~/hamdeck-push` (branch `work`, tracks `origin/linux-port`).

Written because the last build found its gaps one at a time, in use, by the operator. This is
the surface up front.

---

## 1. It is not one feature. It is three, pointing in two directions.

| # | what | direction | port? |
|---|---|---|---|
| A | HTTP **:54321** — `GET /{freq}[/{mode}]` sets the radio | **Wavelog → radio** (click a spot, rig QSYs) | wanted |
| B | WS **:54322** — broadcasts `radio_status` to connected clients | radio → WaveLogGate clients | who consumes this? |
| C | `PostToWavelog()` → `POST {url}/api/radio` | **radio → Wavelog** | **this is the ask** |

Joe's "push data to Wavelog" is **C**. **A** is the reverse direction and is the half that
makes the bandmap useful. They share a class in C# and share nothing else — same lock, same
config, different jobs. Splitting them is free and worth it.

**B is unexplained.** WaveLogGate is a separate program; this reimplements its wire protocol so
WaveLogGate's own clients can attach. Nothing here is known to consume it. **Do not port it
until something asks for it.**

## 2. Verified against the live Wavelog, not the manual

`hamlog.io` → `application/controllers/Api.php:1059  function radio()`. Payload shape confirmed
from the sample on line 1066: `{"radio","frequency","mode","timestamp"}` + `key`. Wavelog keys
its radio row **by the `radio` name string**. ⚠️ `Api.php:1077` calls
`check_rate_limit('radio', $identifier)` — there IS a server-side rate limit, so a pusher that
fires on every VFO click will hit it.

## 3. The bit-by-bit walk — what is wrong with it

Ordered by what would bite us, not by line number.

### 🔴 3a. `PostToWavelog` is SILENT ON FAILURE — the single most important fix
```csharp
if (resp.IsSuccessStatusCode) Logger.Debug("WAVELOG", "Updated ...");   // line 217
catch (Exception ex) { Logger.Debug("WAVELOG", "API error: {0}", ...); } // line 220
```
A wrong key, a 401, a rate-limit rejection, a DNS failure — **all log nothing at all**, and even
the success path is `Debug`. The feature can be dead for months and look identical to working.
Same family as last night's muted microphone: every counter healthy, nothing coming out.
**Port requirement:** the last POST's status/time/body is a first-class, readable piece of state,
and a failing pusher is visible without reading a log.

### 🔴 3b. It binds `http://+:54321/` — the whole network, unauthenticated, and it keys the VFO
```csharp
listener.Prefixes.Add("http://+:54321/");   // line 66; falls back to localhost only if that THROWS
```
Where the `+` bind succeeds, **anyone on the LAN can retune the radio** with a bare
`GET /14074000`. No auth, no token, nothing. Note the contrast the same repo already draws —
`Models/Config.cs:52` on the CAT proxy: *"loopback only, which is where local tools (Stream Deck
and friends) live anyway."* The Wavelog server does not follow its own house rule.

Compounding it, lines 91-92:
```csharp
resp.Headers.Add("Access-Control-Allow-Origin", "*");
resp.Headers.Add("Access-Control-Allow-Private-Network", "true");
```
`Allow-Private-Network` is precisely the browser guard against a public web page reaching into
localhost — waived, for any origin. So **any website open in the browser can QSY the rig too.**
The header is needed for the real case (a Wavelog page reaching the local bridge); the wildcard
is not. Port it **loopback-bound, origin pinned to the configured Wavelog URL.**

### 🟠 3c. No validation on either value it feeds the radio
`long.TryParse(parts[0], out var freq)` → `_radio.SetFreq(freq)`. `/1` is accepted. `/0` is
accepted. `parts[1].ToUpper()` goes to `SetMode` as an arbitrary string. Band-edge and
mode-enum checks are cheap and belong before the CAT write.

### 🟠 3d. The trigger set and the payload set do not match
```csharp
if (freq != _lastFreq || mode != _lastMode || power != _lastPower || tx != _lastTX)  // line 184
```
It posts to Wavelog when **TX state** changes — and `tx` **is not in the payload**. So keying up
and unkeying fires two identical POSTs carrying no new information, straight into 3a's rate
limit. TX belongs in the local broadcast (B), not in the Wavelog trigger.

### 🟠 3e. Debounce is 500 ms with no floor and no coalescing
`UpdateLoop` wakes every 500 ms and posts on any change. Spinning the VFO = a POST every 500 ms
for as long as the knob turns, each one immediately obsolete. Wants a settle window (post the
frequency you **stopped** on) plus a heartbeat for "still here, unchanged".

### 🟡 3f. `radio = "HamDeck"` is hardcoded (line 206)
That string is Wavelog's primary key for the radio row. Two pushers, or a second rig, silently
overwrite one another. **This becomes load-bearing under Joe's two-pusher design — see §5.**

### 🟢 3g. Two comments that are earned lessons — carry them forward
- **Lines 32-37 / 176-182:** `_lastTX` is cached deliberately. `GetTXStatus()` issues a `TX;`
  serial query, so calling it per WebSocket client per broadcast compounds serial traffic and
  fights the UI poll for the radio lock. **Never query the rig from inside a broadcast loop.**
- **Lines 278-281:** `Dispose` must `Close()` the listeners, because `GetContextAsync` ignores
  the cancellation token and would otherwise block forever.

## 4. ⚠️ The part that does NOT port, and would fail silently if we tried

`UpdateLoop` reads `_radio.LastFrequency / LastMode / LastPower / LastTXState` — **caches that
nothing in this class populates.** They are filled by `MainWindow`'s 200 ms WPF UI tick
(`Views/MainWindow.xaml.cs:163` is the only place `WaveLogServer` is even constructed).

**A desktop app has no MainWindow and no `RadioController`.** Lifted as-is, every cached value
stays at its zero, `freq != _lastFreq` never becomes true after the first pass, and the pusher
runs forever posting nothing — no error, no log, service "running". Exactly last night's shape.

**The desktop app's data source is the host's HTTP API, not a radio object.** And the C++ host
already answers better than the C# cache did:

```
GET /api/status → {"connected","freq","mode","vfo","power","tx","cache_age_ms","stale", ...}
```
⚠️ **`stale` + `cache_age_ms` have no C# equivalent** and are the fix for 3a's blind spot: the
C# pusher would cheerfully publish a frozen cached frequency forever if the poll died. **Never
POST to Wavelog on a snapshot where `stale` is true or `connected` is false.**

## 5. 🔴 The new problem the C# never had: TWO pushers

Joe, 08/31: the desktop pusher **is not on 24/7**; when the remote client is in use it reads the
data, and **when the remote client is closed the local push takes over.**

The C# has no concept of this. It posts whenever it sees a change, full stop. So this is **new
work, not a port**, and it is the dangerous part, because the failure is silent in both
directions:

- **Both push at once** → they race on the same Wavelog radio row (3f). Last writer wins; the
  log shows whichever arrived last, which may be the older reading.
- **Neither pushes** → the desktop app is off, the remote client is closed, and Wavelog quietly
  keeps showing the last frequency it was ever told. **A stale value looks exactly like a
  current one.** This is the one to design against: staleness must be visible, not inferred.

**The host is the only thing that is always on and the only thing that knows who is connected.**
So even though the *pusher* is a desktop app (Joe's call), the *arbiter* should be the host: one
place answers "who owns the push right now", and each pusher asks rather than guesses.

⚠️ **The flag we would reach for today is the wrong one.** `/api/tx-audio/status` →
`client_connected` (`src/api.cpp:466`) means **someone is holding the TX audio**, not "a remote
client is connected". Listening remotely, RX only, reads **false**. `/api/admin/sessions` is the
honest list. A real `remote_active` on the host is a prerequisite for this whole feature.

## 6. Stream Deck — there is nothing to port

Grepped the whole C# repo. **No Stream Deck code exists.** `README.md:100` is the entire
implementation:

> **Stream Deck setup:** install the API Ninja plugin and point buttons at …

So today's Stream Deck support is a third-party generic HTTP-request plugin aimed at the REST
API — which the C++ host already serves, ~140 routes, unchanged. `AUDIT.md:135` records that a
CSRF-hardening pass once broke every Stream Deck button on this station, so **whatever we do,
plain unauthenticated GETs from a dumb HTTP plugin are a real constraint.**

That makes Stream Deck a **separate question with a cheap answer and an expensive one**, and it
should not be bundled with the Wavelog work:
- cheap: keep the API Ninja plugin. Zero code. Buttons fire; keys show nothing.
- expensive: a real Elgato plugin so keys **display** state (frequency on the LCD, PTT lit).
  That is a Node/JS plugin against Elgato's SDK — a different language and toolchain from
  everything in this repo.

## 7. Open questions before code

1. **Does the bandmap→QSY half (A) matter to you**, or is this push-only? A is what makes
   clicking a spot in Wavelog retune the rig.
2. **Stream Deck: fire-only or keys that show state?** Decides zero code vs. a new toolchain.
3. **Anything consuming the WS on 54322?** If not, it does not get written.
