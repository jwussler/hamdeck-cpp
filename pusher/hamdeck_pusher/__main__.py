"""CLI. `--once` and `--selftest` exist so this can be PROVEN, not just run.

⚠️ --selftest builds the real objects and exercises the real decision path against a
stub host and a stub Wavelog. A status flag that only reports "running" is the thing
this whole project was written to stop shipping.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from .config import Settings, default_path
from .runner import Runner
from .state import Phase, PushResult


def _selftest(emit=print) -> int:
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
        emit(f"  {'ok  ' if cond else 'FAIL'}  {label}")
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

    emit("")
    if fails:
        emit(f"SELFTEST FAILED: {len(fails)} check(s)")
        return 1
    emit("decision path: all checks passed")
    return 0


def _gui_selftest(emit=print) -> int:
    """Build the REAL window offscreen and read values back out of the widgets.

    ⚠️ This exists because of 0.1.12 and the sliders: a UI that loads is not a UI that
    works, and a frozen bundle that starts is not one that has tkinter in it. PyInstaller
    has shipped an empty bundle on this fleet before - the app "ran" and did nothing.
    """
    try:
        import tkinter as tk
        from .gui import App
    except Exception as e:  # noqa: BLE001
        emit(f"  FAIL  tkinter is not available in this build: {e}")
        return 1
    try:
        root = tk.Tk()
    except Exception as e:  # noqa: BLE001
        # ⚠️ A SKIP HERE ALMOST SHIPPED AS A PASS.
        #
        # A bundle with tkinter but no Tk RUNTIME imports fine and then throws TclError
        # from Tk() - which is indistinguishable from "this machine has no display"
        # unless you check. The first version returned 0 for both, so removing the Tk
        # data from the frozen bundle still exited 0 and CI would have shipped a window
        # that cannot open. Proven by deleting _tk_data and watching it pass.
        #
        # So: no display is a skip ONLY where there genuinely is no display. Anywhere a
        # window could have opened - Windows always, Linux with DISPLAY set - this is a
        # broken build and it fails.
        headless = (os.name != "nt"
                    and not os.environ.get("DISPLAY")
                    and not os.environ.get("WAYLAND_DISPLAY"))
        if headless:
            emit(f"  skip  no display on this machine, window not built ({type(e).__name__})")
            return 0
        emit(f"  FAIL  Tk is present but will not start - the bundle is broken: {e}")
        return 1

    from .state import Phase, State
    fails = []

    def check(label, cond):
        emit(f"  {'ok  ' if cond else 'FAIL'}  {label}")
        if not cond:
            fails.append(label)

    try:
        root.withdraw()
        app = App(root, Settings(radio_name="SELFTEST"), None)
        st = State()
        st.observed_freq, st.observed_mode = 14074000, "USB"
        st.published_freq, st.published_mode = 14074000, "USB"
        st.note(Phase.PUBLISHING, "14.074000 MHz USB")
        st.published_at = __import__("time").time()
        app._paint(st)
        root.update_idletasks()
        check("the window builds and paints", app.state_lbl.cget("text") == "LOGGING")
        check("the radio readout shows the frequency",
              "14.0740" in app.rig_freq.cget("text"))
        check("in sync -> the log readout is lit amber",
              app.log_freq.cget("fg").lower() == "#ffb020")

        st.published_freq = 7190000          # log now behind the radio
        app._paint(st)
        root.update_idletasks()
        check("out of sync -> the log readout is dimmed",
              app.log_freq.cget("fg").lower() == "#8a6320")
        check("and it says so in words", "behind the radio" in app.why.cget("text"))

        st.note(Phase.FAILING, "403 rejected - check the Wavelog API key")
        app._paint(st)
        root.update_idletasks()
        check("a rejected key paints red, not green",
              app.state_lbl.cget("text") == "NOT LOGGING"
              and app.state_lbl.cget("fg").lower() == "#b4232a")

        st.note(Phase.DEFERRED, "a remote client is operating the station")
        app._paint(st)
        root.update_idletasks()
        # ⚠️ Deferring must NOT look like a failure. From Wavelog they are identical;
        # on screen they must not be.
        check("standing down is amber, not red",
              app.state_lbl.cget("fg").lower() == "#ffb020")
    finally:
        try:
            root.destroy()
        except Exception:  # noqa: BLE001
            pass

    if fails:
        emit(f"GUI SELFTEST FAILED: {len(fails)} check(s)")
        return 1
    emit("window: all checks passed")
    return 0


def main(argv=None) -> int:
    # ⚠️ FIRST, before argument parsing, any window, or any network call. Velopack
    # re-invokes this exe with hook arguments during install and update and expects it to
    # act and exit; anything that starts up before this turns an update into a hang.
    from .updates import run_startup_hooks
    run_startup_hooks()

    ap = argparse.ArgumentParser(prog="hamdeck-pusher",
                                 description="Push HamDeck rig state to Wavelog.")
    ap.add_argument("--config", type=Path, default=None)
    ap.add_argument("--once", action="store_true", help="one pass, print the state, exit")
    ap.add_argument("--status", action="store_true", help="print settings (redacted) and exit")
    ap.add_argument("--selftest", action="store_true", help="prove the decision path offline")
    ap.add_argument("--gui", action="store_true", help="the window (default when frozen)")
    ap.add_argument("--report", type=Path, default=None,
                    help="write --selftest output here as well as stdout")
    ap.add_argument("--shot", type=Path, default=None,
                    help="--gui: render, save nothing but exit after N ms (see --shot-ms)")
    ap.add_argument("--shot-ms", type=int, default=1500)
    args = ap.parse_args(argv)

    if args.selftest:
        lines: list[str] = []
        rc = _selftest(lines.append)
        rc |= _gui_selftest(lines.append)
        text = "\n".join(lines)
        # ⚠️ A WINDOWED exe HAS NO stdout. PyInstaller --noconsole discards prints
        # entirely, so CI would see an exit code and nothing else - and the one time
        # that matters is when it fails. --report is how the frozen build says why.
        if args.report:
            args.report.write_text(text + "\n")
        print(text)
        return rc

    if args.gui or getattr(sys, "frozen", False):
        from .gui import run
        return run(args.config, shot=args.shot, shot_ms=args.shot_ms)

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
