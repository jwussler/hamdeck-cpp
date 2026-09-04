#!/usr/bin/env python3
# App Store Connect API client.
#
# ⚠️ CREDENTIALS ARE READ FROM /home/ubuntu/secure/apple AND NEVER PRINTED. That
# directory is not in git and must not be - the .p8 can be downloaded from Apple
# exactly once, so losing it costs one of a capped number of keys.
import json, os, sys, time, urllib.request, urllib.error
import jwt   # PyJWT

SECURE = os.environ.get("APPLE_SECURE_DIR", "/home/ubuntu/secure/apple")
KEY_ID = os.environ.get("ASC_KEY_ID", "8WPAG839Q7")
BASE   = "https://api.appstoreconnect.apple.com"

def _token():
    issuer = open(os.path.join(SECURE, "asc_issuer_id")).read().strip()
    private = open(os.path.join(SECURE, f"AuthKey_{KEY_ID}.p8")).read()
    now = int(time.time())
    # ⚠️ 20 MINUTES IS APPLE'S CEILING. A longer exp is rejected as 401
    # NOT_AUTHORIZED, which reads exactly like a bad key.
    return jwt.encode({"iss": issuer, "iat": now, "exp": now + 19 * 60,
                       "aud": "appstoreconnect-v1"},
                      private, algorithm="ES256", headers={"kid": KEY_ID, "typ": "JWT"})

def call(method, path, body=None):
    url = path if path.startswith("http") else BASE + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + _token())
    if data: req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as e:
        raw = e.read()
        try: return e.code, json.loads(raw)
        except Exception: return e.code, {"raw": raw.decode(errors="replace")[:500]}

def errors(payload):
    return "; ".join(f"{e.get('status')} {e.get('code')}: {e.get('title')} - {e.get('detail','')}"
                     for e in payload.get("errors", [])) or json.dumps(payload)[:400]

if __name__ == "__main__":
    m = sys.argv[1].upper() if len(sys.argv) > 2 else "GET"
    p = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
    b = json.loads(sys.argv[3]) if len(sys.argv) > 3 else None
    st, out = call(m, p, b)
    print(st); print(json.dumps(out, indent=2)[:4000])
