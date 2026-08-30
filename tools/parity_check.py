#!/usr/bin/env python3
"""Compare the C++ host's route contracts against the reference .NET host.

    tools/parity_check.py --reference <host:port> --candidate <host:port> \
                          --user <name> --password <pw>

⚠️ THIS TOOL IS READ-ONLY BY CONSTRUCTION, AND THAT IS NOT A COURTESY.

A parity walker that simply GETs every route would key the transmitter, change
the operating mode and retune the amplifier on a live station - most of this API
is state-changing, and many of those routes are GETs. CARRYOVER.md section 9
records that probing with a control route once changed the operating mode
mid-session, with a human at the radio.

So the safety is STRUCTURAL, not remembered:

  1. Only routes on SAFE_ROUTES below are ever requested. The list is an
     allowlist, copied from the reference host's own ReadOnlyRoutes set.
  2. Every URL is re-checked against DANGEROUS_PATTERNS immediately before the
     request, so a future edit that adds a state-changing route to the allowlist
     still cannot fire one.
  3. There is no --all-routes flag. Adding one would defeat both checks; if a
     state-changing route ever needs comparing, do it against a SIMULATED rig,
     never against the station.

A scope lock the operator has to remember is not a lock.
"""

import argparse
import json
import sys
import urllib.request
import urllib.error

# Copied from ReadOnlyRoutes in the reference host: reads rig state, changes
# nothing. /api/health and /api/auth/status need no session at all.
SAFE_ROUTES = [
    "/api/health",
    "/api/auth/status",
    "/api/status",
    "/api/status/full",
    "/api/meters",
    "/api/freq",
    "/api/freq-b",
    "/api/freq/get",
    "/api/power/limit",
    "/api/volume/get",
    "/api/rf-gain/get",
    "/api/cw-speed/get",
    "/api/ant/get",
    "/api/ant/rx/get",
    "/api/vfo-lock/status",
    "/api/diversity/status",
    "/api/record/status",
    "/api/tx-audio/status",
    "/api/tx-audio/devices",
    "/api/voice/status",
    "/api/cw/status",
    "/api/tune/tgxl/status",
    "/api/tune/amp/status",
    "/api/remote-tx/status",
    "/api/ssb-out-level/get",
]

# Belt and braces, restructured: rather than blocklisting substrings - which
# grew brittle as read-only routes like /api/volume/get and /api/record/status
# were added and kept colliding with it - a route is refused unless its FINAL
# SEGMENT is a read. Everything that acts on the radio ends in a verb.
#
# This is the stronger form: a new state-changing route added to SAFE_ROUTES by
# mistake still cannot fire, because "on"/"toggle"/"tune" are not reads.
READ_SUFFIXES = {
    "health", "status", "full", "meters", "freq", "freq-b", "get", "limit",
    "devices", "spots", "session",
}


# Divergences that are deliberate, with the reason. Listed rather than hidden:
# a tool that quietly suppresses differences stops being a parity check.
KNOWN_DIVERGENCES = {
    "/api/auth/status": (
        "control port only: the reference host registers no auth routes on its "
        "local port and answers 'Unknown route'. The C++ host answers there too, "
        "reporting the caller as authenticated because the local port is trusted. "
        "Deliberate - it keeps one capability reachable through one documented "
        "route on every listener. Both hosts agree on the dashboard port, which "
        "is the one clients use."
    ),
    "/api/cw/status": (
        "extra 'available' and 'reason'. The CW keyer is not ported yet. "
        "CARRYOVER section 1: if a capability is absent its status route must SAY "
        "so - the reference /api/record/start answers ok/recording:true while "
        "Start() sets IsRecording=false. Extra fields are additive and clients "
        "ignore unknown keys, so this is honest without breaking the shape."
    ),
    "/api/tune/tgxl/status": (
        "extra 'available'. TGXL is not configured on this host, and a status "
        "route that only says tuning:false cannot distinguish 'idle' from 'not "
        "there at all'."
    ),
    "/api/tune/amp/status": (
        "extra 'available'. Same reason as the TGXL status route."
    ),
}


def assert_safe(path):
    """Refuse anything that could change the radio. Fails closed."""
    if path not in SAFE_ROUTES:
        sys.exit(f"REFUSING {path}: not on the read-only allowlist")
    tail = path.rstrip("/").split("/")[-1]
    if tail not in READ_SUFFIXES:
        sys.exit(f"REFUSING {path}: final segment {tail!r} is not a read")


def login(base, user, password):
    req = urllib.request.Request(
        f"http://{base}/api/auth/login",
        data=json.dumps({"username": user, "password": password}).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            for k, v in r.getheaders():
                if k.lower() == "set-cookie" and "hamdeck_session=" in v:
                    return v.split("hamdeck_session=")[1].split(";")[0]
    except urllib.error.HTTPError as e:
        sys.exit(f"login to {base} failed: {e.code}")
    sys.exit(f"login to {base} returned no session cookie")


def fetch(base, path, token):
    assert_safe(path)
    req = urllib.request.Request(f"http://{base}{path}",
                                 headers={"Cookie": f"hamdeck_session={token}"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, None
    except Exception as e:                                  # noqa: BLE001
        return 0, {"_error": str(e)}


def shape(obj, prefix=""):
    """Keys and TYPES only. Values are the station's business and differ anyway."""
    if not isinstance(obj, dict):
        return {prefix.rstrip("."): type(obj).__name__}
    out = {}
    for k, v in obj.items():
        if isinstance(v, dict):
            out.update(shape(v, f"{prefix}{k}."))
        else:
            out[f"{prefix}{k}"] = type(v).__name__
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True, help="host:port of the .NET host")
    ap.add_argument("--candidate", required=True, help="host:port of the C++ host")
    ap.add_argument("--user", help="omit when pointing at a local control port, "
                                   "which needs no session")
    ap.add_argument("--password")
    args = ap.parse_args()

    # No credentials means the targets are local control ports, which need no
    # session. Useful over an ssh tunnel, and it keeps a station password out of
    # shell history and CI logs.
    ref_tok = login(args.reference, args.user, args.password) if args.user else ""
    cand_tok = login(args.candidate, args.user, args.password) if args.user else ""

    failures = 0
    for path in SAFE_ROUTES:
        rs, rb = fetch(args.reference, path, ref_tok)
        cs, cb = fetch(args.candidate, path, cand_tok)

        if rs != cs:
            if path in KNOWN_DIVERGENCES:
                print(f"  known  {path}: status {rs} vs {cs}")
                print(f"           {KNOWN_DIVERGENCES[path]}")
                continue
            print(f"  DIFF   {path}: status {rs} vs {cs}")
            failures += 1
            continue
        if rb is None and cb is None:
            print(f"  ok     {path}: both {rs}")
            continue

        rshape, cshape = shape(rb), shape(cb)
        missing = sorted(set(rshape) - set(cshape))
        extra = sorted(set(cshape) - set(rshape))
        retyped = sorted(k for k in set(rshape) & set(cshape)
                         if rshape[k] != cshape[k])
        if (missing or extra or retyped) and path in KNOWN_DIVERGENCES:
            print(f"  known  {path}: differs deliberately")
            print(f"           {KNOWN_DIVERGENCES[path]}")
            continue
        if missing or extra or retyped:
            print(f"  DIFF   {path}")
            for k in missing:
                print(f"           missing: {k} ({rshape[k]})")
            for k in extra:
                print(f"           extra:   {k} ({cshape[k]})")
            for k in retyped:
                print(f"           type:    {k} {rshape[k]} -> {cshape[k]}")
            failures += 1
        else:
            print(f"  ok     {path}: {len(rshape)} fields match")

    print()
    print(f"{len(SAFE_ROUTES) - failures}/{len(SAFE_ROUTES)} routes match "
          f"({len(KNOWN_DIVERGENCES)} known divergence(s) listed above)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
