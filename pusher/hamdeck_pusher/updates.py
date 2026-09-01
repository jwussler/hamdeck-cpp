"""Velopack integration: the install/update hooks, and checking for a new version.

⚠️ VELOPACK IS OPTIONAL AT RUNTIME, ON PURPOSE.

The app has to keep running from a plain source checkout - that is how it is developed,
how it is diagnosed on a machine where the installer is blocked, and what
`Run with console (shows errors).cmd` uses. So every entry point here degrades to a no-op
when the package is not present rather than raising. A packaging dependency that stops the
app starting outside its package is a worse bug than the one it was added to fix.
"""

from __future__ import annotations

try:
    import velopack
    AVAILABLE = True
except Exception:  # noqa: BLE001 - not installed, or a platform without a wheel
    velopack = None
    AVAILABLE = False


#: The Qt client ships inside the same package, beside the pusher.
CLIENT_REL = ("client", "hamdeck-qml.exe")


def _client_path():
    """Where the Qt client lives inside an installed package, or None."""
    import sys
    from pathlib import Path
    # Frozen: sys.executable is <install>\hamdeck-pusher.exe. From source there is no
    # client beside us, and that is fine - this is packaging, not a dependency.
    root = Path(sys.executable).parent if getattr(sys, "frozen", False) else None
    if root is None:
        return None
    p = root.joinpath(*CLIENT_REL)
    return p if p.exists() else None


def _make_client_shortcut(*_args) -> None:
    """Give the Qt client its own Start Menu entry on first run.

    ⚠️ VELOPACK ONLY CREATES SHORTCUTS FOR THE MAIN EXECUTABLE. Two applications now ship
    in one package, so without this the client is installed and reachable only by browsing
    to the folder - which is exactly the "it's in a folder somewhere" outcome this whole
    packaging change existed to get rid of.

    Best effort by design: a failure here must never stop the app starting. A missing
    shortcut is an annoyance; an app that will not launch after an update is not.
    """
    import os
    client = _client_path()
    if client is None or os.name != "nt":
        return
    try:
        import subprocess
        start_menu = os.path.join(os.environ["APPDATA"], "Microsoft", "Windows",
                                  "Start Menu", "Programs")
        link = os.path.join(start_menu, "HamDeck Remote.lnk")
        # PowerShell rather than pywin32: no new dependency, and WScript.Shell is present
        # on every Windows that can run this.
        ps = (f"$s=(New-Object -ComObject WScript.Shell).CreateShortcut('{link}');"
              f"$s.TargetPath='{client}';"
              f"$s.WorkingDirectory='{client.parent}';$s.Save()")
        subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", ps],
                       check=False, timeout=20,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except Exception:  # noqa: BLE001
        pass


def run_startup_hooks() -> None:
    """⚠️ MUST BE THE FIRST THING main() DOES, BEFORE ANY UI OR NETWORK.

    Velopack re-invokes the installed exe with hook arguments at install, update and
    uninstall time, and expects it to handle them and exit. If the app instead opens a
    window or starts polling the host during an update, the update stalls behind a running
    process that was never meant to start - which looks like a hung installer, not like a
    missing call.
    """
    if not AVAILABLE:
        return
    try:
        velopack.App().on_first_run(_make_client_shortcut).run()
    except Exception:  # noqa: BLE001 - never let a hook failure stop the app starting
        pass


def current_version() -> str | None:
    if not AVAILABLE:
        return None
    try:
        return velopack.UpdateManager(_source()).get_current_version()
    except Exception:  # noqa: BLE001
        return None


#: Where releases live. PUBLIC and source-free, which is the whole point: the app reads it
#: with NO ACCESS TOKEN.
RELEASES_URL = "https://github.com/jwussler/hamdeck-releases"


def _source():
    """Where updates come from.

    ⚠️ NO ACCESS TOKEN, AND THAT IS THE DESIGN. `GithubSource` accepts one, and using it
    here would put a credential inside every copy handed to a friend - readable straight
    back out of the package. The releases repository is public precisely so this argument
    stays empty.

    The source repository stays private; only built artifacts are public. That also keeps
    station detail out of view - the carryover doc alone carries a LAN address and account
    names.
    """
    return velopack.GithubSource(RELEASES_URL)


def check() -> "tuple[bool, str]":
    """(update_available, human explanation). NEVER raises.

    ⚠️ An update check that throws takes the app down for a problem that is not the app's
    job. A failed check means "carry on running", not "stop" - the pusher's actual work is
    publishing to Wavelog, and it must survive GitHub being unreachable.
    """
    if not AVAILABLE:
        return False, "updates unavailable (not an installed build)"
    try:
        mgr = velopack.UpdateManager(_source())
        info = mgr.check_for_updates()
        if info is None:
            return False, f"up to date ({mgr.get_current_version()})"
        return True, f"update available: {info.TargetFullRelease.Version}"
    except Exception as e:  # noqa: BLE001
        return False, f"update check failed: {type(e).__name__}: {e}"


def download_and_apply() -> str:
    """Download a pending update and restart into it. Returns why if it did not.

    ⚠️ Only ever called from an explicit user action. An app that restarts itself
    unprompted is an app that vanishes mid-transmission.
    """
    if not AVAILABLE:
        return "updates unavailable (not an installed build)"
    try:
        mgr = velopack.UpdateManager(_source())
        info = mgr.check_for_updates()
        if info is None:
            return "already up to date"
        mgr.download_updates(info)
        mgr.apply_updates_and_restart(info)
        return "restarting"
    except Exception as e:  # noqa: BLE001
        return f"update failed: {type(e).__name__}: {e}"
