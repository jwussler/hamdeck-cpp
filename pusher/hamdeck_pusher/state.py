"""What the pusher is doing right now, and why.

⚠️ THIS MODULE IS THE WHOLE POINT OF THE REWRITE.

The C# server it replaces logged a success at debug level and logged nothing at all on
failure, so "pushing fine", "the API key is wrong" and "deliberately standing down" were
one indistinguishable silence. From Wavelog's side they still are: the radio row simply
stops changing.

So the pusher's condition is a value, not a log line. Every decision it makes lands here
with a reason, and the tray, the CLI and --selftest all read the same object.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import Enum


class Phase(str, Enum):
    """Why the pusher is or is not publishing. Ordered worst-to-best for display."""

    FAILING = "failing"          # tried to publish and the attempt failed
    NO_HOST = "no_host"          # cannot reach or log in to the HamDeck host
    RIG_DOWN = "rig_down"        # host is up, radio is not connected
    STALE = "stale"              # host's reading is too old to publish
    DEFERRED = "deferred"        # a remote client is operating; standing down on purpose
    IDLE = "idle"                # nothing to say yet
    PUBLISHING = "publishing"    # last publish succeeded


#: Phases where Wavelog is NOT being updated but nothing is wrong. Kept explicit so
#: nobody has to infer intent from an enum name.
BENIGN = frozenset({Phase.DEFERRED, Phase.IDLE})


@dataclass
class PushResult:
    ok: bool
    status: int | None
    detail: str
    at: float = field(default_factory=time.time)


@dataclass
class State:
    phase: Phase = Phase.IDLE
    reason: str = "not started"
    #: Last reading actually PUBLISHED, not merely observed. The difference matters:
    #: observing 14.074 and failing to publish it must not make the UI claim 14.074 is
    #: in the log.
    published_freq: int | None = None
    published_mode: str | None = None
    published_at: float | None = None
    last_result: PushResult | None = None
    consecutive_failures: int = 0
    #: Last reading OBSERVED on the radio, published or not. Kept separate from the
    #: published pair on purpose: the whole question this app answers is whether the log
    #: is following the radio, and you cannot see that from one number.
    observed_freq: int | None = None
    observed_mode: str | None = None

    @property
    def in_sync(self) -> bool:
        """Does Wavelog currently match the radio?"""
        return (self.observed_freq is not None
                and (self.observed_freq, self.observed_mode)
                == (self.published_freq, self.published_mode))

    def note(self, phase: Phase, reason: str) -> None:
        self.phase = phase
        self.reason = reason

    def record_push(self, result: PushResult, freq: int, mode: str) -> None:
        self.last_result = result
        if result.ok:
            self.consecutive_failures = 0
            self.published_freq = freq
            self.published_mode = mode
            self.published_at = result.at
            self.note(Phase.PUBLISHING, f"{freq/1e6:.6f} MHz {mode}")
        else:
            self.consecutive_failures += 1
            # ⚠️ The reason carries the SERVER's own words. "failed" on its own sends
            # somebody to read code; "403" or "wrong api key" sends them to the setting.
            self.note(Phase.FAILING, result.detail)

    @property
    def publishing(self) -> bool:
        return self.phase is Phase.PUBLISHING

    @property
    def healthy(self) -> bool:
        """True when Wavelog is current OR deliberately not being updated."""
        return self.phase is Phase.PUBLISHING or self.phase in BENIGN

    def age_seconds(self) -> float | None:
        """How long since anything was actually published."""
        return None if self.published_at is None else time.time() - self.published_at

    def summary(self) -> str:
        bits = [self.phase.value.upper(), self.reason]
        age = self.age_seconds()
        if age is not None:
            bits.append(f"last published {int(age)}s ago")
        elif self.phase is not Phase.IDLE:
            # ⚠️ Say this plainly. A pusher that has run for an hour and published
            # nothing is the exact failure the C# hid.
            bits.append("nothing published yet")
        return " · ".join(bits)
