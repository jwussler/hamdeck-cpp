"""CLI. `--once` and `--selftest` exist so this can be PROVEN, not just run.

⚠️ --selftest builds the real objects and exercises the real decision path against a
stub host and a stub Wavelog. A status flag that only reports "running" is the thing
this whole project was written to stop shipping.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .config import Settings, default_path
from .runner import Runner
from .state import Phase, PushResult


def _selftest() -> int:
    """Drive the decision path end to end with no network. Prints what it proved."""
    from .policy import Reading

    class StubHost:
        def __init__(self):
            self.status_doc = {"connected": True, "freq": 14074000, "mode": "USB",
                               "power": 100, "stale": False, "cache_age_ms": 100}
            self.remote_doc = {"active": False}
        def status(self): return self.status_doc
        def remote_status(self): return self.remote_doc

    class StubWavelog:
        def __init__(self): self.posts = []; self.ok = True
        def post(self, freq, mode, power=None):
            self.posts.append((freq, mode, power))
            return PushResult(self.ok, 200 if self.ok else 403,
                              "ok" if self.ok else "403 rejected - check the Wavelog API key")

    t = [1000.0]
    s = Settings(host_url="stub", host_user="u", host_password="p",
                 wavelog_url="stub", wavelog_key="k", settle_seconds=2.0,
                 heartbeat_seconds=300.0)
    host, wl = StubHost(), StubWavelog()
    r = Runner(s, host=host, wavelog=wl, clock=lambda: t[0])

    fails = []
    def check(label, cond):
        print(f"  {'ok  ' if cond else 'FAIL'}  {label}")
        if not cond:
            fails.append(label)

    r.tick()
    check("a brand-new reading waits for the settle window", not wl.posts)
    t[0] += 3
    r.tick()
    check("publishes once it holds still", wl.posts == [(14074000, "USB", 100)])

    t[0] += 1
    host.remote_doc = {"active": True}
    host.status_doc = dict(host.status_doc, freq=14200000)
    r.tick()
    check("stands down while a remote client operates",
          r.state.phase is Phase.DEFERRED and len(wl.posts) == 1)
    check("deferring is reported as healthy, not as a failure", r.state.healthy)

    t[0] += 30
    host.remote_doc = {"active": False}
    r.tick()
    check("takes the station back the instant the client closes - no extra wait",
          len(wl.posts) == 2 and wl.posts[-1][0] == 14200000)

    t[0] += 5
    host.status_doc = dict(host.status_doc, stale=True, cache_age_ms=8000)
    r.tick()
    check("refuses to publish a stale reading",
          r.state.phase is Phase.STALE and len(wl.posts) == 2)

    t[0] += 5
    host.status_doc = dict(host.status_doc, stale=False, freq=21300000)
    wl.ok = False
    # ⚠️ Two ticks, deliberately. A STALE reading does not start the settle clock - it
    # cannot, we do not know it is real - so the first fresh reading after staleness is
    # a brand-new candidate and still has to hold still. Writing this as one tick made
    # the selftest fail against correct code, which is the right way round.
    r.tick()
    t[0] += 3
    r.tick()
    check("a rejected post is reported with the reason, not swallowed",
          r.state.phase is Phase.FAILING and "API key" in r.state.reason)
    t[0] += 5
    r.tick()
    check("and is RETRIED rather than counted as published", len(wl.posts) == 4)

    wl.ok = True
    t[0] += 5
    r.tick()
    check("recovers once the key is fixed", r.state.publishing)

    print()
    if fails:
        print(f"SELFTEST FAILED: {len(fails)} check(s)")
        return 1
    print("selftest: all checks passed")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hamdeck-pusher",
                                 description="Push HamDeck rig state to Wavelog.")
    ap.add_argument("--config", type=Path, default=None)
    ap.add_argument("--once", action="store_true", help="one pass, print the state, exit")
    ap.add_argument("--status", action="store_true", help="print settings (redacted) and exit")
    ap.add_argument("--selftest", action="store_true", help="prove the decision path offline")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    settings = Settings.load(args.config)
    if args.status:
        print(f"config: {args.config or default_path()}")
        for k, v in settings.redacted().items():
            print(f"  {k}: {v}")
        for p in settings.problems():
            print(f"  ⚠️  {p}")
        return 0

    problems = settings.problems()
    if problems:
        for p in problems:
            print(f"⚠️  {p}", file=sys.stderr)
        print(f"\nEdit {args.config or default_path()}", file=sys.stderr)
        return 2

    runner = Runner(settings)
    if args.once:
        try:
            print(runner.tick().summary())
            return 0 if runner.state.healthy else 1
        finally:
            # ⚠️ Always, even on the error path. A session left behind is the ghost the
            # next run defers to.
            runner.host.logout()
    try:
        runner.run(on_change=lambda st: print(st.summary(), flush=True))
    except KeyboardInterrupt:
        pass
    finally:
        runner.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
