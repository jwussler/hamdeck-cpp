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
        velopack.App().run()
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
