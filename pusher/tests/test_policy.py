"""The publish policy, refusal by refusal.

⚠️ This is the gate for a feature whose failures are SILENT. From Wavelog's side a
pusher that is deferring, a pusher whose API key is wrong, and a pusher republishing a
frozen frequency all look the same: the radio row stops changing, or changes to
something wrong and stays there. Nothing here can be checked by looking at it.

Each test names the C# defect it exists to prevent.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from hamdeck_pusher.policy import PushPolicy, Reading  # noqa: E402
from hamdeck_pusher.state import Phase  # noqa: E402

GOOD = {"connected": True, "freq": 14074000, "mode": "USB", "power": 100,
        "stale": False, "cache_age_ms": 120}


def reading(**over):
    d = dict(GOOD)
    d.update(over)
    return Reading.from_status(d)


class Refusals(unittest.TestCase):
    def setUp(self):
        self.p = PushPolicy(settle_seconds=2.0, heartbeat_seconds=300.0)

    def test_host_unreachable_is_not_a_publish(self):
        d = self.p.evaluate(None, False, 100.0, host_error="ConnectionRefusedError")
        self.assertFalse(d.publish)
        self.assertIs(d.phase, Phase.NO_HOST)

    def test_rig_disconnected(self):
        d = self.p.evaluate(reading(connected=False), False, 100.0)
        self.assertFalse(d.publish)
        self.assertIs(d.phase, Phase.RIG_DOWN)

    def test_stale_reading_is_never_published(self):
        """THE defect the C# could not even detect - it had no staleness signal."""
        d = self.p.evaluate(reading(stale=True, cache_age_ms=9000), False, 100.0)
        self.assertFalse(d.publish)
        self.assertIs(d.phase, Phase.STALE)
        self.assertIn("9000", d.reason)

    def test_missing_stale_field_is_treated_as_stale(self):
        """A host too old to report it must not be assumed fresh."""
        r = Reading.from_status({"connected": True, "freq": 14074000, "mode": "USB"})
        self.assertTrue(r.stale)
        self.assertFalse(self.p.evaluate(r, False, 100.0).publish)

    def test_zero_frequency_is_not_a_reading(self):
        d = self.p.evaluate(reading(freq=0), False, 100.0)
        self.assertFalse(d.publish)

    def test_defers_to_a_remote_client(self):
        d = self.p.evaluate(reading(), True, 100.0)
        self.assertFalse(d.publish)
        self.assertIs(d.phase, Phase.DEFERRED)

    def test_deferral_is_reported_as_benign_not_as_failure(self):
        """⚠️ 'Nobody is publishing' and 'publishing is failing' must not merge."""
        from hamdeck_pusher.state import BENIGN
        self.assertIn(Phase.DEFERRED, BENIGN)
        self.assertNotIn(Phase.FAILING, BENIGN)
        self.assertNotIn(Phase.STALE, BENIGN)

    def test_deferral_can_be_turned_off(self):
        p = PushPolicy(settle_seconds=0.0, defer_to_remote=False)
        self.assertTrue(p.evaluate(reading(), True, 100.0).publish)


class Pacing(unittest.TestCase):
    def setUp(self):
        self.p = PushPolicy(settle_seconds=2.0, heartbeat_seconds=300.0)

    def test_a_spinning_vfo_does_not_publish_every_tick(self):
        """The C# posted on any change every 500ms - a post per tick while tuning,
        each obsolete on arrival, straight into Wavelog's rate limit."""
        published = 0
        t = 0.0
        for step in range(20):                      # 20 tuning ticks, 0.25s apart
            d = self.p.evaluate(reading(freq=14074000 + step * 100), False, t)
            if d.publish:
                published += 1
            t += 0.25
        self.assertEqual(published, 0, "published while the operator was still tuning")

    def test_publishes_once_the_reading_holds_still(self):
        r = reading(freq=14200000)
        self.assertFalse(self.p.evaluate(r, False, 0.0).publish)
        self.assertFalse(self.p.evaluate(r, False, 1.0).publish)
        d = self.p.evaluate(r, False, 2.5)
        self.assertTrue(d.publish)
        self.assertIs(d.phase, Phase.PUBLISHING)

    def test_unchanged_reading_does_not_republish_immediately(self):
        r = reading()
        self.p.evaluate(r, False, 0.0)
        self.assertTrue(self.p.evaluate(r, False, 3.0).publish)
        self.p.record_published(r, 3.0)
        self.assertFalse(self.p.evaluate(r, False, 10.0).publish)

    def test_heartbeat_republishes_so_last_updated_stays_honest(self):
        r = reading()
        self.p.evaluate(r, False, 0.0)
        self.p.evaluate(r, False, 3.0)
        self.p.record_published(r, 3.0)
        self.assertFalse(self.p.evaluate(r, False, 200.0).publish)
        d = self.p.evaluate(r, False, 400.0)
        self.assertTrue(d.publish)
        self.assertIn("heartbeat", d.reason)

    def test_tx_and_power_alone_never_cause_a_publish(self):
        """⚠️ The C# triggered on TX changes while not sending tx in the payload -
        two identical posts per over, carrying nothing new."""
        r1 = reading(power=5)
        self.p.evaluate(r1, False, 0.0)
        self.p.evaluate(r1, False, 3.0)
        self.p.record_published(r1, 3.0)
        # ⚠️ Check PAST the settle window too. Only checking the instant after the
        # change lets a policy that wrongly treats power as a trigger slip through -
        # it would merely be waiting to settle, and would publish a moment later.
        self.assertFalse(self.p.evaluate(reading(power=100), False, 4.0).publish,
                         "a power change alone caused a Wavelog post")
        self.assertFalse(self.p.evaluate(reading(power=100), False, 30.0).publish,
                         "a power change alone caused a Wavelog post once it settled")

    def test_a_failed_publish_is_retried_not_forgotten(self):
        """⚠️ Recording an ATTEMPT instead of a success is how a pusher convinces
        itself the log is current while every post is being rejected."""
        r = reading(freq=21300000)
        self.p.evaluate(r, False, 0.0)
        self.assertTrue(self.p.evaluate(r, False, 3.0).publish)
        # the POST failed, so record_published is NOT called
        self.assertTrue(self.p.evaluate(r, False, 4.0).publish,
                        "gave up on a reading that was never actually published")


class Handoff(unittest.TestCase):
    def test_the_station_comes_back_when_the_remote_client_closes(self):
        p = PushPolicy(settle_seconds=1.0)
        r = reading(freq=7190000)
        self.assertIs(p.evaluate(r, True, 0.0).phase, Phase.DEFERRED)
        self.assertIs(p.evaluate(r, True, 50.0).phase, Phase.DEFERRED)
        d = p.evaluate(r, False, 60.0)          # client closed
        self.assertTrue(d.publish, "did not take the station back after the client left")


if __name__ == "__main__":
    unittest.main(verbosity=2)


class GhostSessions(unittest.TestCase):
    """⚠️ A helper deferring to its OWN previous session.

    Measured on the live host: run the pusher, let it exit, run it again inside the
    activity window, and it reports "a remote client is operating the station" with
    nothing else connected at all. Under a crash-restart loop it would never publish
    again - silently, because deferring looks exactly like working.
    """

    @staticmethod
    def active(doc):
        from hamdeck_pusher.runner import Runner
        return Runner._remote_active(doc)

    def test_nobody_there(self):
        self.assertFalse(self.active(
            {"active": False, "other_clients": 0, "same_user_clients": 0, "tx_holder": ""}))

    def test_our_own_ghost_is_not_an_operator(self):
        self.assertFalse(self.active(
            {"active": True, "other_clients": 1, "same_user_clients": 1, "tx_holder": ""}))

    def test_a_real_operator_on_another_account_is(self):
        self.assertTrue(self.active(
            {"active": True, "other_clients": 1, "same_user_clients": 0, "tx_holder": ""}))

    def test_a_real_operator_alongside_our_ghost_is(self):
        self.assertTrue(self.active(
            {"active": True, "other_clients": 2, "same_user_clients": 1, "tx_holder": ""}))

    def test_tx_holder_wins_regardless_of_the_counts(self):
        """Somebody holding the transmitter is operating, whoever they logged in as."""
        self.assertTrue(self.active(
            {"active": True, "other_clients": 1, "same_user_clients": 1, "tx_holder": "joe"}))

    def test_an_older_host_falls_back_to_deferring(self):
        """⚠️ Missing field -> defer. Two publishers racing on one Wavelog row is a
        worse outcome than one that stands down when it need not."""
        self.assertTrue(self.active({"active": True, "other_clients": 1, "tx_holder": ""}))
