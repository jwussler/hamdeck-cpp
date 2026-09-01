"""The Stream Deck endpoint.

⚠️ THIS EXISTS SO EXISTING BUTTONS KEEP WORKING, NOT SO NEW ONES CAN BE MADE.

From the C# host's AUDIT.md §11: "Every Stream Deck button on this station targets
localhost:5001; all 44 of them, with no LAN address anywhere." That worked because the C#
host RAN ON THE STATION PC, so localhost:5001 was the host. The rig moved to its own box
and that URL became nothing - which is why the deck went dead, and why the answer is to
make the same URL answer again rather than to reconfigure 44 buttons.

Measured from the station PC before this existed:

    GET :5002/api/status    -> 401   (the real host needs a session)
    GET :5002/api/agc/fast  -> 401
    GET :5001/api/status    -> unreachable (control listener is loopback-only ON THE HOST)

An API Ninja button cannot log in and cannot hold a session. This can, so it does: it
holds one session and lends it to whatever fires at loopback.

⚠️ THE TRUST MODEL IS THE LOOPBACK BIND, AND NOTHING ELSE. Anything that can reach this
port can drive the radio with no password - which is exactly the deal the C# host made,
deliberately, for the same reason. So it binds 127.0.0.1 explicitly (a kernel guarantee,
not a check somebody can forget) AND refuses a non-loopback peer if it ever finds one.
It is off by default: `deck_port: 0`.
"""

from __future__ import annotations

import ipaddress
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .hostclient import HostClient, HostError

#: The port the existing buttons use. Changing this means editing 44 buttons.
DEFAULT_PORT = 5001


class _Handler(BaseHTTPRequestHandler):
    server_version = "HamDeckPusher"
    # The default logger writes a line per request to stderr, which on a windowed build
    # goes nowhere and on a console build is 44 buttons' worth of noise.
    def log_message(self, *a):  # noqa: D102
        pass

    def do_GET(self):  # noqa: N802
        proxy = self.server.proxy  # type: ignore[attr-defined]

        # ⚠️ Belt as well as braces. The bind below is the real guarantee; this catches
        # the case where somebody "helpfully" changes the bind address one day.
        try:
            peer = ipaddress.ip_address(self.client_address[0])
            if not peer.is_loopback:
                self._json(403, '{"status":"error","message":"loopback only"}')
                return
        except ValueError:
            self._json(403, '{"status":"error","message":"loopback only"}')
            return

        path = self.path
        if not path.startswith("/api/"):
            # No index page, no favicon, no static anything. This is not a web server.
            self._json(404, '{"status":"error","message":"only /api/ paths are proxied"}')
            return

        try:
            status, body = proxy.host.passthrough(path)
        except HostError as e:
            # ⚠️ 502, not 500, and it says the host is the problem. A button that fails
            # because the rig box is down must not look like a button that is wrong.
            self._json(502, '{"status":"error","message":"host unreachable: %s"}'
                       % e.detail.replace('"', "'"))
            proxy.note_failure(e.detail)
            return
        proxy.note_ok()
        self._json(status, body)

    def _json(self, status: int, body: str):
        raw = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class _Server(ThreadingHTTPServer):
    # ⚠️ WINDOWS LETS A SECOND PROCESS BIND A PORT THAT IS ALREADY IN USE.
    #
    # http.server sets allow_reuse_address = 1, which on Linux only shortens TIME_WAIT
    # and still refuses a live second listener. On Windows the same flag means "share
    # it": a second copy of this app binds 5001 with NO ERROR, and Stream Deck requests
    # then go to whichever socket happens to win. Two copies fighting over 44 buttons,
    # silently - the exact case the "port already in use" message was written for, not
    # working on the one platform that has the Stream Deck attached.
    #
    # Found by CI, which failed on Windows while passing on Linux. So: off on Windows,
    # where it is a hazard; on elsewhere, where it only helps a quick restart.
    allow_reuse_address = os.name != "nt"


class DeckProxy:
    """Serves loopback GETs by replaying them against the host with a real session."""

    def __init__(self, host: HostClient, port: int = DEFAULT_PORT):
        self.host = host
        self.port = port
        self.requests = 0
        self.failures = 0
        self.last_error: str | None = None
        self._srv: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    def note_ok(self):
        self.requests += 1
        self.last_error = None

    def note_failure(self, detail: str):
        self.requests += 1
        self.failures += 1
        self.last_error = detail

    def start(self) -> str:
        """Returns a human sentence about what happened. Never raises."""
        try:
            # ⚠️ 127.0.0.1, never 0.0.0.0. This port has no authentication at all; the
            # bind address IS the security.
            self._srv = _Server(("127.0.0.1", self.port), _Handler)
        except OSError as e:
            # ⚠️ Say WHICH port and WHY. "Failed to start" sends somebody to read code;
            # "port 5001 is already in use" sends them to close the other program - and
            # the other program is very often an older copy of this one.
            return f"Stream Deck endpoint OFF - port {self.port}: {e.strerror or e}"
        self._srv.proxy = self  # type: ignore[attr-defined]
        self._thread = threading.Thread(target=self._srv.serve_forever, daemon=True)
        self._thread.start()
        return f"Stream Deck endpoint on http://127.0.0.1:{self.port}/api/…"

    def stop(self):
        if self._srv:
            self._srv.shutdown()
            self._srv.server_close()
            self._srv = None
