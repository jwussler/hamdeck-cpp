"""The GUI selftest's own honesty.

⚠️ THIS TEST EXISTS BECAUSE THE GATE ALMOST SHIPPED AS A NO-OP.

The first version caught every exception from `tk.Tk()` and returned 0 with the word
"skip". That is right on a headless build box and catastrophically wrong on a Windows
runner: a frozen bundle missing its Tk runtime imports tkinter fine and then throws
TclError from Tk() - the exact failure the gate exists to catch - and it was reported as
a skip and passed. A check that cannot tell "no display" from "the window cannot open" is
not a check.
"""

import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from hamdeck_pusher.__main__ import _gui_selftest  # noqa: E402


class TkRefusesToStart(unittest.TestCase):
    """Tk imports but will not open a window - a broken bundle, or no display."""

    def _run(self, env):
        import tkinter
        with mock.patch.dict(os.environ, env, clear=False):
            for k in ("DISPLAY", "WAYLAND_DISPLAY"):
                if k not in env:
                    os.environ.pop(k, None)
            with mock.patch.object(tkinter, "Tk",
                                   side_effect=tkinter.TclError("no display name")):
                out = []
                return _gui_selftest(out.append), "\n".join(out)

    @unittest.skipIf(os.name == "nt", "posix display variables")
    def test_headless_box_skips(self):
        """No DISPLAY at all: honest skip, exit 0."""
        rc, text = self._run({})
        self.assertEqual(rc, 0)
        self.assertIn("skip", text)

    @unittest.skipIf(os.name == "nt", "posix display variables")
    def test_a_display_exists_but_tk_will_not_start_is_a_FAILURE(self):
        """⚠️ THE ONE THAT MATTERS. A display is available, so Tk failing means the
        build is broken - and on Windows this branch is always the live one."""
        rc, text = self._run({"DISPLAY": ":99"})
        self.assertEqual(rc, 1, "a broken Tk runtime was reported as a pass")
        self.assertIn("FAIL", text)
        self.assertIn("bundle is broken", text)

    def test_tkinter_missing_entirely_is_always_a_failure(self):
        """PyInstaller leaving tkinter out of the bundle - never a skip."""
        with mock.patch.dict(sys.modules, {"tkinter": None}):
            out = []
            rc = _gui_selftest(out.append)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL", "\n".join(out))


if __name__ == "__main__":
    unittest.main(verbosity=2)
