"""The loop: read the host, decide, publish, and always say what happened."""

from __future__ import annotations

import threading
import time

from .config import Settings
from .deck import DeckProxy
from .hostclient import HostClient, HostError
from .policy import PushPolicy, Reading
from .state import Phase, State
from .wavelog import WavelogClient


class Runner:
    def __init__(self, settings: Settings, host: HostClient | None = None,
                 wavelog: WavelogClient | None = None, clock=time.time):
        self.settings = settings
        self.state = State()
        self.clock = clock
        self.host = host or HostClient(settings.host_url, settings.host_user,
                                       settings.host_password)
        self.wavelog = wavelog or WavelogClient(settings.wavelog_url, settings.wavelog_key,
                                                settings.radio_name)
        self.policy = PushPolicy(settle_seconds=settings.settle_seconds,
                                 heartbeat_seconds=settings.heartbeat_seconds,
                                 defer_to_remote=settings.defer_to_remote)
        self._stop = threading.Event()
        # ⚠️ The proxy SHARES this runner's HostClient, and therefore its session. That
        # is the entire point - a Stream Deck button cannot log in, so it borrows the
        # session something else is already keeping alive. It also means one re-login
        # serves both, rather than two racing to refresh the same account.
        self.deck: DeckProxy | None = None
        self.deck_status = "Stream Deck endpoint off"

    # ── one pass, so it can be tested and run once from the CLI ──────────────
    def tick(self) -> State:
        now = self.clock()
        reading = None
        remote_active = False
        host_error = None
        try:
            reading = Reading.from_status(self.host.status())
            # ⚠️ Asked SEPARATELY and every pass. Cached "is anyone else there" is how a
            # helper keeps transmitting deference long after the client left, or worse,
            # stops deferring while it is still connected.
            remote_active = self._remote_active(self.host.remote_status())
        except HostError as e:
            host_error = e.detail

        if reading is not None and reading.connected and reading.freq:
            self.state.observed_freq = reading.freq
            self.state.observed_mode = reading.mode

        decision = self.policy.evaluate(reading, remote_active, now, host_error)
        if not decision.publish:
            self.state.note(decision.phase, decision.reason)
            return self.state

        assert reading is not None
        result = self.wavelog.post(reading.freq, reading.mode, reading.power)
        self.state.record_push(result, reading.freq, reading.mode)
        if result.ok:
            # ⚠️ ONLY on success. Recording an attempt would reset the heartbeat and the
            # change test, so a rejected post is never retried and the pusher believes
            # the log is current.
            self.policy.record_published(reading, now)
        return self.state

    @staticmethod
    def _remote_active(doc: dict) -> bool:
        """Is a REAL operator there - as opposed to this helper's own leftovers?

        ⚠️ `active` alone is not enough. A previous run of this program leaves a session
        behind until it ages out, and to the host that is simply another session. So a
        restart reads active=true against nothing but its own ghost, stands down, and
        under a crash-restart loop stands down forever. The host reports how many of the
        others share the caller's username; subtract those.

        A host too old to report the field falls back to `active`, which is the
        conservative direction: defer rather than risk two publishers racing.
        """
        if not doc.get("active"):
            return False
        if doc.get("tx_holder"):
            return True                       # somebody is holding the transmitter
        if "same_user_clients" not in doc:
            return True
        return (int(doc.get("other_clients", 0))
                - int(doc.get("same_user_clients", 0))) > 0

    # ── the long-running form ────────────────────────────────────────────────
    def start_deck(self) -> str:
        if self.deck or not self.settings.deck_port:
            return self.deck_status
        # ⚠️ Log in FIRST. Otherwise the first button press pays for the login, and an
        # API Ninja button that times out looks like a dead button rather than a slow one.
        try:
            if not self.host.logged_in:
                self.host.login()
        except HostError as e:
            self.deck_status = f"Stream Deck endpoint OFF - cannot log in: {e.detail}"
            return self.deck_status
        self.deck = DeckProxy(self.host, self.settings.deck_port)
        self.deck_status = self.deck.start()
        return self.deck_status

    def stop_deck(self):
        if self.deck:
            self.deck.stop()
            self.deck = None
        self.deck_status = "Stream Deck endpoint off"

    def run(self, on_change=None) -> None:
        self.start_deck()
        last = None
        while not self._stop.is_set():
            try:
                self.tick()
            except Exception as e:  # noqa: BLE001 - a loop that dies is worse than a bad pass
                self.state.note(Phase.FAILING, f"{type(e).__name__}: {e}")
            summary = self.state.summary()
            if on_change and summary != last:
                on_change(self.state)
                last = summary
            self._stop.wait(self.settings.poll_seconds)

    def stop(self) -> None:
        self._stop.set()
        self.stop_deck()
        self.host.logout()
