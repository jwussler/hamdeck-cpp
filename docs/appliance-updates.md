# Appliance updates — design

For the Raspberry Pi build. The audience is **technical but not programmers**: people who can
read a status page and follow four numbered steps, and who cannot recover a bricked Pi sitting
behind a rig with no console.

Everything below hangs off one principle:

> **A bad update must never be able to take the station off the air.**

Nothing here is built yet. This is the design, written while it was fresh.

---

## 1. A/B slots, using the Pi's own `tryboot`

Two root partitions. An update is written to the **inactive** one and never touches the running
system. Then `tryboot` — Pi firmware supports this natively — does a **one-shot** boot into the
new slot. If the new version does not explicitly confirm itself, the next reboot returns to the
old slot on its own.

No RAUC, no Mender, no container runtime. The platform already does this, and it is designed for
exactly this case.

**Why not the alternatives:** apt gives partial updates and no atomic rollback; containers add a
runtime and a lot of weight for a box whose whole appeal is being small.

## 2. The health gate — what counts as "it worked"

The new version has to earn the switch. Within **120 seconds** of boot:

- the service is running
- `/api/health` answers
- **`rig_connected` matches what it was before the update**
- the audio device is present

All pass → confirm the slot permanently. Anything fails → reboot to the old slot and record why.

⚠️ **Compare the rig against its previous state, not against `true`.** If the rig was unplugged
before the update, failing the update over it would roll back a perfectly good version and hide
the real problem. This is the same mistake shape as measuring latency by inference instead of
asking the kernel: an absolute check that looks right and is wrong.

## 3. When an update is allowed to run

Strictest part of the design.

- **Never while transmitting.** Check `tx` first.
- **Never while a client is connected.** Check the session count.
- **Only after N minutes idle**, inside a maintenance window the operator sets.

An appliance that reboots mid-QSO is unforgivable, and "it was 3 a.m." is not a defence during a
contest.

## 4. Consent scales with risk

| change | behaviour |
|---|---|
| patch — 0.17.1 → 0.17.2 | automatic, overnight |
| minor — 0.17 → 0.18 | automatic, announced in the UI beforehand |
| major — 0.x → 1.0 | **requires a click** |

Auto-update is a **security feature** here: these are internet-facing boxes (see
`docs/exposure.md` — many operators will forward a port) owned by people who will never patch by
hand. Unpatched is the greater risk. But a silent change to PTT or audio behaviour across a major
version is a different thing entirely, and gets a human.

## 5. Two non-negotiables

**Updates must be signed**, public key baked into the image, verified before a single byte is
written. Without that, auto-update *is* a remote-code-execution feature — precisely what the
port-forwarding design is trying to avoid.

**The admin page shows the truth**: current version, what is available, when it last checked, and
the result of the last attempt including any rollback and its reason.

## 6. Failure modes, all of which must be boring

| what happens | result |
|---|---|
| no internet | do nothing; do not retry aggressively |
| partial download | checksum fails, discard, try next window |
| power lost mid-write | inactive slot is corrupt, active slot untouched; retry later |
| new version will not boot | `tryboot` reverts on the next boot with no human involved |
| new version boots but is unhealthy | health gate fails, reboot to old slot, report why |

**The worst realistic outcome is "still running the old version, and telling you why."** That is a
support email, not a dead station.
