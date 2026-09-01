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


def _source():
    """Where updates come from.

    ⚠️ NOT WIRED YET, AND THAT IS A DECISION NOT AN OMISSION. `GithubSource` takes
    (repo_url, access_token, prerelease) - and `hamdeck-cpp` is a PRIVATE repo, so an
    anonymous client cannot read its releases. Shipping a token inside the binary to work
    around that would put a credential in every copy handed to a friend, where it can be
    read straight back out.

    The same fact bites one step earlier than updates, too: a friend cannot download the
    installer from a private repo at all. So the update source and the download location
    are the same unanswered question, and it is Joe's to answer - make the repo public, or
    host releases on his own domain (HttpSource takes any HTTPS directory).
    """
    raise NotImplementedError("update source not chosen yet - see the docstring")
