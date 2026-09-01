"""Posting rig state to Wavelog.

Verified against the live install, not the docs: `application/controllers/Api.php`,
`function radio()` at line 1059, sample payload on 1066.

    POST {base}/api/radio
    {"key": ..., "radio": ..., "frequency": <Hz>, "mode": ..., "timestamp": "Y/m/d H:i"}

⚠️ Api.php:1077 calls check_rate_limit('radio', $identifier). There IS a server-side
limit, so a pusher that fires on every VFO click will be throttled. Pacing is in
pusher.py, not here.
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

from .hostclient import USER_AGENT
from .state import PushResult


class WavelogClient:
    def __init__(self, base_url: str, api_key: str, radio_name: str, timeout: float = 5.0):
        self.base = base_url.rstrip("/")
        self.api_key = api_key
        # ⚠️ NOT hardcoded, unlike the C# which always sent "HamDeck". Wavelog keys its
        # radio row by this string, so two pushers sharing a name overwrite each other
        # and the log shows whichever POST happened to land last.
        self.radio_name = radio_name
        self.timeout = timeout

    def post(self, freq: int, mode: str, power: int | None = None) -> PushResult:
        """Publish one reading. NEVER raises - the result is the point."""
        if not self.base or not self.api_key:
            return PushResult(False, None, "not configured: set the Wavelog URL and API key")

        payload = {
            "key": self.api_key,
            "radio": self.radio_name,
            "frequency": freq,
            "mode": mode,
            # Minute resolution, UTC - the format Api.php's own sample uses.
            "timestamp": datetime.now(timezone.utc).strftime("%Y/%m/%d %H:%M"),
        }
        # The C# sent power only when >0. Keep that: 0 W is not a reading, it is a rig
        # that has not answered yet.
        if power:
            payload["power"] = power

        req = urllib.request.Request(
            f"{self.base}/api/radio", data=json.dumps(payload).encode(), method="POST")
        req.add_header("Content-Type", "application/json")
        # ⚠️ Without this, Cloudflare in front of hamlog.io returns 403 error 1010 and
        # the failure looks like a rejected API key.
        req.add_header("User-Agent", USER_AGENT)

        started = time.time()
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read().decode("utf-8", "replace")
                return _interpret(resp.status, body, started)
        except urllib.error.HTTPError as e:
            return _interpret(e.code, e.read().decode("utf-8", "replace"), started)
        except Exception as e:  # noqa: BLE001
            # ⚠️ REPORTED, not swallowed. The C# caught this and logged at debug, so a
            # pusher that could not resolve the hostname looked exactly like one that
            # was working.
            return PushResult(False, None, f"{type(e).__name__}: {e}", started)


def _interpret(status: int, body: str, at: float) -> PushResult:
    """Turn a response into a verdict that names the setting to go and fix."""
    snippet = body.strip()[:200]
    if status == 403 and "1010" in body:
        return PushResult(False, status, "403 from Cloudflare (1010) - request sent no "
                                         "User-Agent", at)
    if status == 401 or status == 403:
        return PushResult(False, status, f"{status} rejected - check the Wavelog API key", at)
    if status == 429:
        return PushResult(False, status, "429 rate limited by Wavelog - slow the push down", at)
    if status != 200:
        return PushResult(False, status, f"HTTP {status}: {snippet}", at)

    # ⚠️ A 200 IS NOT A SUCCESS HERE. Wavelog answers 200 with a JSON body that says
    # what it actually did, and its import paths report failure inside a 200 - the
    # NetLogger sync learned this the expensive way on /api/qso.
    try:
        data = json.loads(body)
    except json.JSONDecodeError:
        return PushResult(False, status, f"200 but body was not JSON: {snippet}", at)
    if isinstance(data, dict):
        state = str(data.get("status", "")).lower()
        if state in {"failed", "error", "abort"}:
            reason = data.get("reason") or data.get("message") or snippet
            return PushResult(False, status, f"Wavelog refused it: {reason}", at)
    return PushResult(True, status, "ok", at)
