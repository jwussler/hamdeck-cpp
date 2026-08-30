# Telling the operator about updates — design and copy

Two components update independently: the **client** on a laptop, and the **host** on the Pi. They
can be different versions, and that mismatch has already caused a real support incident.

The audience is technical but not programmers. **For this feature the copy is the feature** — the
notification is trivial, the instructions are the part that decides whether they act.

---

## 1. The client can see both versions — use that

The client knows its own version and gets the host's from `/api/health`. So it can distinguish
three situations that otherwise feel identical to a user:

1. **the client is behind** → download link
2. **the host is behind** → link to the station's own admin page, where the update happens
3. **they are incompatible** → and *what will break*

⚠️ **Case 3 is the one worth building.** When host v3.4.14 tightened receiver audio to require a
session, the older client's RX path had not been updated. The symptom was:

> `RX audio failed: The server returned status code '401' when status code '101' was expected`

That is a protocol error for what was actually a version mismatch, and it cost hours. A line
saying *"this station runs 3.4.14, your client is 0.4.2 — receiver audio needs an update"* would
have collapsed it to nothing.

**Add a `min_client_version` field to `/api/health`** so the host states its own requirement
rather than the client inferring it from version numbers. A rule the host declares holds; a rule
the client guesses rots.

---

## 2. Host update that needs approval

The client cannot do it for them, so it walks them through and then confirms it worked.

> **Your station has an update that needs your approval**
> Station: `hamdeck.local` · running 0.17.1 · 1.0.0 available
>
> This is a major update, so it won't install on its own.
>
> 1. Open your station's page — **[hamdeck.local]**
> 2. Sign in with your callsign and password
> 3. Click **Install update**
> 4. It takes about two minutes and restarts itself
>
> You can keep operating until you're ready. Nothing changes until you click.

Then the client **watches for it to come back**:

> Station restarting… reconnecting automatically.
> ✅ Reconnected. Station now running 1.0.0.

That last part matters more than it looks. It turns a reboot they would otherwise stare at into a
progress indicator. Most of the anxiety here is not the update — it is not knowing whether it is
working or whether they have broken something.

---

## 3. New client available

> **A new HamDeck client is available** — 0.18.0 (you have 0.17.1)
> [Download] [What's new] [Not now]
>
> Your saved stations, audio devices and PTT key are kept.

⚠️ **That last line does more work than the rest combined.** The commonest reason people leave a
working setup alone is fear of reconfiguring it — and here it is *true* that settings survive,
because they live in `%APPDATA%\HamDeckRemote\settings.json` and the installer only writes the
exe. Say it.

---

## 4. Behaviour

- **Check once a day**, not on every launch. GitHub's releases API is enough.
- **Remember dismissals per version.** One notice per release, never one per start.
- **A status line, never a modal.** Nothing pops a dialog in front of someone mid-QSO.
- **Do not auto-update the client.** It needs elevation, and today it would be downloading
  *unsigned* installers — a worse habit than a manual click. Revisit once signing is done.
- Make the update check **opt-out**; it is a call to a third party from the operator's machine.

---

## 5. Two rules for all of this copy

**Never "please", never "sorry", never "an error occurred".** If an update fails, say what
happened and what to do:

> Update rolled back — the radio didn't reconnect. Your station is still running 0.17.1 and is
> working. Check the USB cable and try again.

**Always give the elapsed expectation.** "About two minutes" is what prevents someone rebooting
*during* the reboot, which is how appliances actually get broken.

Same register as the rest of the project: specific, measured, unembarrassed about limits.
