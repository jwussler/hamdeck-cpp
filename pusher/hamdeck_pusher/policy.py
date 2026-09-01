"""WHEN to publish. No I/O, so it can be tested exhaustively - which it is.

Every rule here is a defect found in the C# it replaces (see ../AUDIT-WAVELOG.md).
The order matters and is deliberate: the refusals come first, so a reading has to
survive all of them before anything reaches the log.
"""

from __future__ import annotations

from dataclasses import dataclass

from .state import Phase


@dataclass(frozen=True)
class Reading:
    """One /api/status snapshot, reduced to what a log cares about."""

    connected: bool
    freq: int
    mode: str
    power: int
    stale: bool
    cache_age_ms: int

    @classmethod
    def from_status(cls, d: dict) -> "Reading":
        return cls(
            connected=bool(d.get("connected")),
            freq=int(d.get("freq") or 0),
            mode=str(d.get("mode") or ""),
            power=int(d.get("power") or 0),
            # ⚠️ Default TRUE. A host too old to report staleness must be treated as
            # possibly stale, never as fresh. Defaulting the other way turns a missing
            # field into a confident wrong answer.
            stale=bool(d.get("stale", True)),
            cache_age_ms=int(d.get("cache_age_ms", -1)),
        )

    @property
    def key(self) -> tuple[int, str]:
        """What counts as "a different reading" for publishing purposes.

        ⚠️ POWER AND TX ARE NOT IN HERE, DELIBERATELY. The C# triggered a Wavelog POST
        on TX-state changes while not sending tx in the payload at all, so keying up and
        unkeying fired two identical posts carrying nothing new, straight into Wavelog's
        rate limit. Power rides along with a post; it does not cause one.
        """
        return (self.freq, self.mode)


@dataclass(frozen=True)
class Decision:
    publish: bool
    phase: Phase
    reason: str


@dataclass
class PushPolicy:
    #: Seconds a reading must hold still before it is worth publishing. Spinning the
    #: VFO must not generate a post per tick, each one obsolete on arrival.
    settle_seconds: float = 2.0
    #: Republish an unchanged reading this often, so Wavelog's "last updated" stays
    #: honest instead of ageing silently.
    heartbeat_seconds: float = 300.0
    #: Stand down while a remote client is operating. Joe's rule, 08/31/2026.
    defer_to_remote: bool = True

    _candidate: tuple[int, str] | None = None
    _candidate_since: float = 0.0
    _published: tuple[int, str] | None = None
    _published_at: float = 0.0

    def evaluate(self, reading: Reading | None, remote_active: bool, now: float,
                 host_error: str | None = None) -> Decision:
        # ── refusals, in order ────────────────────────────────────────────────
        if host_error is not None:
            return Decision(False, Phase.NO_HOST, host_error)
        if reading is None:
            return Decision(False, Phase.NO_HOST, "no reading from the host")
        if not reading.connected:
            return Decision(False, Phase.RIG_DOWN, "host is up but the radio is not connected")
        if reading.stale:
            # ⚠️ The refusal the C# could not make: it had no staleness signal and would
            # republish a frozen frequency forever if its poll died. A stale value looks
            # exactly like a current one from Wavelog's side.
            age = (f"{reading.cache_age_ms} ms" if reading.cache_age_ms >= 0
                   else "unknown age")
            return Decision(False, Phase.STALE, f"host reading is stale ({age})")
        if reading.freq <= 0:
            return Decision(False, Phase.STALE, "host reported no frequency")
        # ── the settle window ─────────────────────────────────────────────────
        # ⚠️ TRACKED EVEN WHILE DEFERRING, and that ordering is the whole point.
        #
        # If the deferral returned before this, the settle clock would start from zero
        # the moment a remote client closed - so the handoff would sit out another
        # settle window before publishing anything. That is precisely the wrong moment
        # to add a delay: nothing has been published for the whole length of the
        # deferral, so the log is at its most out of date exactly when the station comes
        # back. A reading that held still throughout the deferral has already settled;
        # it should go out the instant we are allowed to send it.
        if reading.key != self._candidate:
            self._candidate = reading.key
            self._candidate_since = now
        held_for = now - self._candidate_since

        if remote_active and self.defer_to_remote:
            # Standing down ON PURPOSE. Distinct from every failure above, because from
            # Wavelog's side they are identical: the row simply stops changing.
            return Decision(False, Phase.DEFERRED, "a remote client is operating the station")

        if reading.key != self._published:
            if held_for < self.settle_seconds:
                return Decision(False, Phase.IDLE,
                                f"waiting for the reading to settle ({held_for:.1f}s)")
            return Decision(True, Phase.PUBLISHING, "reading changed")

        # ── unchanged: heartbeat only ─────────────────────────────────────────
        since = now - self._published_at
        if since >= self.heartbeat_seconds:
            return Decision(True, Phase.PUBLISHING,
                            f"heartbeat ({int(since)}s since the last publish)")
        return Decision(False, Phase.IDLE, "unchanged")

    def record_published(self, reading: Reading, now: float) -> None:
        """Call ONLY after a publish actually succeeded.

        ⚠️ Recording an attempt rather than a success is how a pusher convinces itself
        the log is current while every post is being rejected: the heartbeat resets, the
        change test passes, and nothing ever retries.
        """
        self._published = reading.key
        self._published_at = now
