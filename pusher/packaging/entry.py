"""PyInstaller entry point.

⚠️ IT IMPORTS THE WHOLE RUNTIME AT TOP LEVEL, ON PURPOSE.

PyInstaller froze an EMPTY bundle on this fleet before, because the package's __main__
deferred all its imports and the analyser therefore found nothing to bundle. The app
started, did nothing, and a `--status` check passed on the broken build because it only
touched the stdlib. Naming every module here is what makes the analyser see them.

Do not "tidy" these into the function that uses them.
"""

import hamdeck_pusher            # noqa: F401
import hamdeck_pusher.config     # noqa: F401
import hamdeck_pusher.gui        # noqa: F401
import hamdeck_pusher.hostclient # noqa: F401
import hamdeck_pusher.policy     # noqa: F401
import hamdeck_pusher.runner     # noqa: F401
import hamdeck_pusher.state      # noqa: F401
import hamdeck_pusher.theme      # noqa: F401
import hamdeck_pusher.wavelog    # noqa: F401
import tkinter                   # noqa: F401
import tkinter.font              # noqa: F401
import tkinter.messagebox        # noqa: F401

from hamdeck_pusher.__main__ import main

if __name__ == "__main__":
    raise SystemExit(main())
