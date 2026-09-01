"""The Stream Deck endpoint.

⚠️ The whole security model is "it only listens on loopback and has no auth". That is the
same deal the C# host made deliberately, and it is only safe while both halves hold, so
both halves are asserted here rather than trusted to a bind string somebody may edit.
"""

import json
import sys
import unittest
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from hamdeck_pusher.deck import DeckProxy  # noqa: E402
from hamdeck_pusher.hostclient import HostError  # noqa: E402


class FakeHost:
    """Stands in for the rig box. Records what the proxy asked it for."""

    def __init__(self):
        self.seen = []
        self.reply = (200, '{"status":"ok"}')
        self.raise_with = None

    def passthrough(self, path):
        self.seen.append(path)
        if self.raise_with:
            raise HostError(self.raise_with)
        return self.reply


def get(port, path):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=3) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


class Proxying(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()
        self.proxy = DeckProxy(self.host, port=0)   # port 0 = let the OS pick
        msg = self.proxy.start()
        self.assertNotIn("OFF", msg, msg)
        self.port = self.proxy._srv.server_address[1]
        self.addCleanup(self.proxy.stop)

    def test_a_button_reaches_the_host_unchanged(self):
        """The existing 44 buttons send exactly these paths. They must pass through
        verbatim - a proxy that rewrites paths is a proxy that needs 44 buttons edited."""
        for path in ("/api/status", "/api/agc/fast", "/api/freq/set/14074000",
                     "/api/mode/usb", "/api/band/40", "/api/freq/digit/7"):
            status, _ = get(self.port, path)
            self.assertEqual(status, 200)
            self.assertEqual(self.host.seen[-1], path)

    def test_the_hosts_own_status_code_is_passed_back(self):
        """⚠️ A 403 from the host must arrive as a 403. Flattening every answer to 200
        would make a refused transmit look like a successful one."""
        self.host.reply = (403, '{"status":"error","message":"Transmit not permitted"}')
        status, body = get(self.port, "/api/ptt/on")
        self.assertEqual(status, 403)
        self.assertIn("Transmit not permitted", body)

    def test_an_unreachable_host_is_502_and_says_so(self):
        """A button failing because the rig box is down must not look like a bad button."""
        self.host.raise_with = "ConnectionRefusedError: [Errno 111]"
        status, body = get(self.port, "/api/status")
        self.assertEqual(status, 502)
        self.assertIn("host unreachable", body)
        self.assertEqual(self.proxy.failures, 1)

    def test_non_api_paths_are_refused(self):
        """Not a web server. No index, no favicon, no static anything."""
        for path in ("/", "/favicon.ico", "/index.html", "/../etc/passwd"):
            status, _ = get(self.port, path)
            self.assertEqual(status, 404)
        self.assertEqual(self.host.seen, [], "a non-API path reached the host")

    def test_it_binds_loopback_only(self):
        """⚠️ THE ONE THAT MATTERS. This port has no authentication whatsoever, so the
        bind address is the entire security model. If it ever listens on 0.0.0.0, anyone
        on the LAN can key the transmitter."""
        self.assertEqual(self.proxy._srv.server_address[0], "127.0.0.1")


class BothLoopbackFamilies(unittest.TestCase):
    """⚠️ THE ONE THAT BROKE IT IN THE FIELD.

    The 44 existing buttons target http://localhost:5001/api/… and on Windows `localhost`
    resolves to ::1 BEFORE 127.0.0.1. A client that does not fall back to IPv4 gets
    connection refused from a listener running perfectly well on 127.0.0.1 - so the app
    reports the endpoint as up and every button fails. The C# host used HttpListener,
    whose "localhost" prefix covers both families, so it never showed up there.
    """

    def setUp(self):
        self.host = FakeHost()
        # A fixed port, because the two listeners must share ONE port number - the whole
        # point is that both families answer on the same URL.
        import socket as _s
        probe = _s.socket(); probe.bind(("127.0.0.1", 0))
        self.port = probe.getsockname()[1]; probe.close()
        self.proxy = DeckProxy(self.host, port=self.port)
        msg = self.proxy.start()
        self.assertNotIn("OFF", msg, msg)
        self.addCleanup(self.proxy.stop)

    def test_ipv4_loopback_answers(self):
        self.assertEqual(get(self.port, "/api/status")[0], 200)

    @unittest.skipUnless(__import__("socket").has_ipv6, "no IPv6 on this machine")
    def test_ipv6_loopback_answers(self):
        import urllib.request
        with urllib.request.urlopen(
                f"http://[::1]:{self.port}/api/status", timeout=3) as r:
            self.assertEqual(r.status, 200)

    @unittest.skipUnless(__import__("socket").has_ipv6, "no IPv6 on this machine")
    def test_the_ipv6_listener_is_loopback_only(self):
        """⚠️ ::1 is as local as 127.0.0.1. Binding :: would put an unauthenticated
        transmitter control on every interface the box has."""
        self.assertEqual(self.proxy._srv6.server_address[0], "::1")


class PortInUse(unittest.TestCase):
    def test_a_taken_port_says_which_port_and_why(self):
        """⚠️ Very often the other program is an older copy of this one. 'Failed to
        start' sends somebody to read code; naming the port sends them to close it."""
        host = FakeHost()
        first = DeckProxy(host, port=0)
        first.start()
        port = first._srv.server_address[1]
        self.addCleanup(first.stop)

        second = DeckProxy(host, port=port)
        msg = second.start()
        self.assertIn("OFF", msg)
        self.assertIn(str(port), msg)
        self.assertIsNone(second._srv)


if __name__ == "__main__":
    unittest.main(verbosity=2)
