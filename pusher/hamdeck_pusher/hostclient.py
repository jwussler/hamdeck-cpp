"""Talking to the HamDeck host.

The host is on the rig box; this runs on the station PC. That means the DASHBOARD
listener (:5002) and a real session - the control listener (:5001) is loopback-only on
the host and is not reachable from here. Measured 09/01/2026:

    GET :5002/api/status   -> 401   (even status needs a session)
    GET :5001/api/status   -> unreachable

⚠️ The session is the reason this helper exists at all. A Stream Deck HTTP plugin cannot
log in or keep a session alive, so without something holding one, nothing on this PC can
reach the rig.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request

#: ⚠️ Cloudflare answers 403 (error 1010) to any client that sends no User-Agent. That
#: bit the NetLogger->Wavelog sync too. It costs one header; omitting it costs an evening.
USER_AGENT = "HamDeckPusher/0.1 (+https://github.com/jwussler/hamdeck-cpp)"


class HostError(RuntimeError):
    """Anything that stopped us getting an answer. Carries the server's own words."""

    def __init__(self, detail: str, status: int | None = None):
        super().__init__(detail)
        self.detail = detail
        self.status = status


class HostClient:
    def __init__(self, base_url: str, username: str, password: str, timeout: float = 5.0):
        self.base = base_url.rstrip("/")
        self.username = username
        self.password = password
        self.timeout = timeout
        self._token: str | None = None

    # ── plumbing ──────────────────────────────────────────────────────────────
    def _request(self, method: str, path: str, body: dict | None = None,
                 with_session: bool = True) -> tuple[int, str, dict]:
        url = f"{self.base}{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("User-Agent", USER_AGENT)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        if with_session and self._token:
            req.add_header("Cookie", f"hamdeck_session={self._token}")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                return resp.status, resp.read().decode("utf-8", "replace"), dict(resp.headers)
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8", "replace"), dict(e.headers)
        except Exception as e:  # noqa: BLE001 - connection refused, DNS, timeout
            raise HostError(f"{type(e).__name__}: {e}") from e

    def _json(self, method: str, path: str, body: dict | None = None) -> dict:
        status, text, _ = self._request(method, path, body)
        # ⚠️ A 401 means the session died, not that the host is broken. Log in once and
        # retry exactly once - not in a loop, or a wrong password becomes a login storm
        # against a host that deliberately delays failed attempts.
        if status == 401:
            self.login()
            status, text, _ = self._request(method, path, body)
        if status != 200:
            raise HostError(_message_from(text, f"HTTP {status}"), status)
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            raise HostError(f"HTTP {status}: response was not JSON", status) from None

    # ── session ───────────────────────────────────────────────────────────────
    def login(self) -> None:
        status, text, headers = self._request(
            "POST", "/api/auth/login",
            {"username": self.username, "password": self.password},
            with_session=False)
        if status != 200:
            raise HostError(_message_from(text, f"login failed (HTTP {status})"), status)
        # ⚠️ The token arrives as a Set-Cookie, NOT in the JSON body. Reading the body
        # gets you a cheerful {"status":"ok"} and no session.
        cookie = headers.get("Set-Cookie", "")
        token = ""
        for part in cookie.split(";"):
            if part.strip().startswith("hamdeck_session="):
                token = part.strip().split("=", 1)[1]
                break
        if not token:
            raise HostError("login returned no session cookie", status)
        self._token = token

    @property
    def logged_in(self) -> bool:
        return self._token is not None

    # ── the two questions this helper asks ────────────────────────────────────
    def status(self) -> dict:
        """Rig state. Carries `stale` and `cache_age_ms` - honour them."""
        return self._json("GET", "/api/status")

    def remote_status(self) -> dict:
        """Is somebody else operating the station?

        ⚠️ NOT /api/tx-audio/status. `client_connected` there means somebody holds the
        TX audio, so an operator listening on RX reads false and this helper would talk
        over the client it meant to yield to.
        """
        return self._json("GET", "/api/remote/status")

    def logout(self) -> None:
        """End the session on the way out.

        ⚠️ NOT politeness. A session that outlives the process is a GHOST: the next run
        starts inside the activity window, sees a session that is not its own token, and
        concludes an operator is at the station - so it stands down against itself.
        Measured on the live host: a second run within 15s reported
        "a remote client is operating the station" with nothing else connected at all.
        """
        if not self._token:
            return
        try:
            self._request("POST", "/api/auth/logout")
        except HostError:
            pass  # going away anyway; the window clears it
        finally:
            self._token = None

    def passthrough(self, path: str) -> tuple[int, str]:
        """Run one host route on behalf of a local caller (the Stream Deck)."""
        status, text, _ = self._request("GET", path)
        if status == 401:
            self.login()
            status, text, _ = self._request("GET", path)
        return status, text


def _message_from(text: str, fallback: str) -> str:
    """Prefer the server's own message. It is nearly always the useful sentence."""
    try:
        msg = json.loads(text).get("message")
        return f"{fallback}: {msg}" if msg else fallback
    except Exception:  # noqa: BLE001
        return fallback
