"""Settings, and where they live.

⚠️ NO DEFAULT HOST, EVER. The C# client shipped one operator's hostname prefilled until
0.4.1, which on a public repo made every install a pointer at that station. Same rule
here: the repo ships no station's address and no key.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, asdict
from pathlib import Path


def default_path() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("APPDATA", Path.home()))
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "HamDeckPusher" / "settings.json"


@dataclass
class Settings:
    host_url: str = ""             # e.g. http://192.168.40.64:5002
    host_user: str = ""
    host_password: str = ""
    wavelog_url: str = ""          # e.g. https://hamlog.io
    wavelog_key: str = ""
    #: ⚠️ Wavelog keys its radio row by this NAME. Two publishers sharing one name
    #: overwrite each other and the log shows whichever POST landed last. Name it for
    #: the station, not the software.
    radio_name: str = "HamDeck"
    poll_seconds: float = 1.0
    settle_seconds: float = 2.0
    heartbeat_seconds: float = 300.0
    defer_to_remote: bool = True
    #: The loopback port the Stream Deck fires at. 0 disables it entirely - the endpoint
    #: needs no auth, so it must be an opt-in, not something a default turns on.
    deck_port: int = 0

    @classmethod
    def load(cls, path: Path | None = None) -> "Settings":
        p = path or default_path()
        if not p.exists():
            return cls()
        raw = json.loads(p.read_text())
        known = {f for f in cls.__dataclass_fields__}
        return cls(**{k: v for k, v in raw.items() if k in known})

    def save(self, path: Path | None = None) -> Path:
        p = path or default_path()
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_suffix(".tmp")
        tmp.write_text(json.dumps(asdict(self), indent=2) + "\n")
        # ⚠️ This file holds two passwords. Tighten it BEFORE the rename, so it is never
        # briefly world-readable under its real name.
        try:
            os.chmod(tmp, 0o600)
        except OSError:
            pass
        tmp.replace(p)
        return p

    def problems(self) -> list[str]:
        out = []
        if not self.host_url:
            out.append("host_url is not set")
        if not self.host_user or not self.host_password:
            out.append("host_user / host_password are not set")
        if not self.wavelog_url:
            out.append("wavelog_url is not set")
        if not self.wavelog_key:
            out.append("wavelog_key is not set")
        if self.settle_seconds < 0 or self.poll_seconds <= 0:
            out.append("poll_seconds must be > 0 and settle_seconds >= 0")
        return out

    def redacted(self) -> dict:
        d = asdict(self)
        for k in ("host_password", "wavelog_key"):
            d[k] = "<set>" if d[k] else "<empty>"
        return d
