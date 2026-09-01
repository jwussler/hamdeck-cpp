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
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .hostclient import HostClient, HostError

#: The port the existing buttons use. Changing this means editing 44 buttons.
DEFAULT_PORT = 5001

#: Filled in by DeckProxy so the WSAEACCES advice can name the real port.
PORT_HINT = DEFAULT_PORT


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


def _explain(e: OSError) -> str:
    """Turn a bind failure into the sentence that names the actual fix.

    ⚠️ WSAEACCES IS THE ONE THAT MISLEADS. Windows words it "an attempt was made to
    access a socket in a way forbidden by its access permissions", which reads like a
    firewall or an antivirus problem and sends the operator to the wrong place entirely.
    It almost never is. On Windows, Hyper-V, WSL and Docker Desktop reserve large blocks
    of TCP ports, and NOTHING can bind inside them - the port is not in use, it is
    spoken for. `netsh interface ipv4 show excludedportrange protocol=tcp` shows the
    blocks, and reserving the port back is the fix.

    That matters more here than usual: the port cannot simply be changed, because 44
    Stream Deck buttons are pointed at 5001.
    """
    win = getattr(e, "winerror", None)
    if win == 10013:            # WSAEACCES
        return ("Windows refuses this port (WSAEACCES). It is usually RESERVED by "
                "Hyper-V / WSL / Docker, not in use. Check with:  netsh interface ipv4 "
                "show excludedportrange protocol=tcp  - and reserve it back with:  "
                "net stop winnat  /  netsh int ipv4 add excludedportrange protocol=tcp "
                f"startport={PORT_HINT} numberofports=1 store=persistent  /  net start winnat")
    if win == 10048 or getattr(e, "errno", None) == 98:   # WSAEADDRINUSE / EADDRINUSE
        return "already in use - another program has it, very often an older copy of this one"
    return str(e.strerror or e)


class _Server6(ThreadingHTTPServer):
    """The IPv6 loopback half. See DeckProxy.start."""
    address_family = socket.AF_INET6
    allow_reuse_address = os.name != "nt"


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
        self._srv6: ThreadingHTTPServer | None = None
        self._threads: list[threading.Thread] = []

    def note_ok(self):
        self.requests += 1
        self.last_error = None

    def note_failure(self, detail: str):
        self.requests += 1
        self.failures += 1
        self.last_error = detail

    def start(self) -> str:
        """Returns a human sentence about what happened. Never raises."""
        global PORT_HINT
        PORT_HINT = self.port
        try:
            # ⚠️ 127.0.0.1, never 0.0.0.0. This port has no authentication at all; the
            # bind address IS the security.
            self._srv = _Server(("127.0.0.1", self.port), _Handler)
        except OSError as e:
            # ⚠️ Say WHICH port and WHY. "Failed to start" sends somebody to read code;
            # "port 5001 is already in use" sends them to close the other program - and
            # the other program is very often an older copy of this one.
            return f"Stream Deck endpoint OFF - port {self.port}: {_explain(e)}"
        self._srv.proxy = self  # type: ignore[attr-defined]
        self._serve(self._srv)

        # ⚠️ AND ::1, BECAUSE THE BUTTONS SAY "localhost".
        #
        # The existing 44 buttons target http://localhost:5001/api/… and on Windows
        # `localhost` resolves to ::1 FIRST. A client that does not fall back to IPv4 -
        # and the Stream Deck plugin is one - gets connection refused from a listener
        # that is running perfectly well on 127.0.0.1. The C# host used HttpListener,
        # whose "localhost" prefix covers both families, so this never came up there.
        #
        # Still loopback-only, so the security model is unchanged: ::1 is as local as
        # 127.0.0.1 is.
        try:
            self._srv6 = _Server6(("::1", self.port), _Handler)
            self._srv6.proxy = self  # type: ignore[attr-defined]
            self._serve(self._srv6)
        except OSError:
            # A box with IPv6 disabled is fine - 127.0.0.1 still answers. Not worth a
            # warning, and definitely not worth failing the whole endpoint over.
            self._srv6 = None

        both = "127.0.0.1 and [::1]" if self._srv6 else "127.0.0.1 only (no IPv6)"
        return f"Stream Deck endpoint on http://localhost:{self.port}/api/… — {both}"

    def _serve(self, srv):
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        self._threads.append(t)

    def stop(self):
        for attr in ("_srv", "_srv6"):
            srv = getattr(self, attr)
            if srv:
                srv.shutdown()
                srv.server_close()
                setattr(self, attr, None)
        self._threads.clear()
