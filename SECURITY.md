# Security

HamDeck keys a transmitter over a network. That is not a normal web app threat model,
so this page says plainly what is protected, what is not, and what must never be
exposed. Read it before putting any part of this on a public address.

## Reporting a problem

Open a GitHub issue for anything already public. For something that would let a
stranger transmit on someone else's licence, **do not open an issue** — use GitHub's
private vulnerability reporting on this repository so there is time to ship a fix.

## The trust model, in one line

**The host is the authority. The client is a display that asks.** Every limit that
matters — the transmit watchdog, the local power cap, who may transmit at all — lives
in the host, because a client can be closed, crashed, or driven from a laptop that
went to sleep mid-transmission.

## What is authenticated

| surface | who can reach it |
|---|---|
| dashboard port, `/ws`, `/ws/tx` | session required (login, or a `hamdeck_session` cookie) |
| `/api/health` | **no session** — liveness and tuner state only, deliberately |
| the API port | intended for **loopback**: local tools such as Stream Deck |
| `/api/admin/*` | an admin account |
| amp tune | local console, or an account explicitly marked as the station |

⚠️ **`allow_anonymous_status` removes the session requirement from receive audio.**
It exists for a reason and it is off by default. Turning it on to make a page work is
the wrong fix — log in instead.

## What must not be exposed

- **Do not put the API port on a LAN address or the internet.** It is designed for
  loopback. An earlier .NET version of this project bound its Wavelog bridge to
  `http://+:54321/` with no authentication at all, where a bare `GET /14074000`
  retuned the radio. That is the mistake this section exists to prevent.
- **Put any remote access behind a tunnel or a reverse proxy that terminates TLS.**
  The host speaks plain HTTP and WebSockets and does not pretend otherwise.
- **Do not expose the CAT proxy.** It is a serial port to the radio wearing a socket.

## Safety properties you should not remove

- **The transmit watchdog** drops PTT after `ptt_timeout_seconds` (default 180) and
  confirms with the radio that it actually stopped. Without it, a dropped link leaves
  the rig keyed with nobody watching.
- **Power returns to the local cap when a remote client disconnects**, so nobody walks
  up to a radio and drives an amplifier with twice the power they expect.
- **Recording is off unless a path is configured**, and PTT auto-record is off unless
  it is switched on. It records whoever you are talking to.

## Licensed-operator responsibility

Remote operation does not change whose callsign is on the air. Control of the
transmitter, and the obligation to identify and to stay in band and licence class, stay
with the operator. Nothing here supervises that for you.
