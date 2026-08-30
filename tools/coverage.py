#!/usr/bin/env python3
"""Report which reference-host routes the C++ host implements.

    tools/coverage.py <host:port>

Reads reference/routes-csharp.txt (the routes extracted from the reference
host's own dispatch table) and probes each one.

⚠️ Same safety rule as walk_all_routes.py: this fires state-changing routes, so
it refuses to run against anything that does not prove simulated:true via
/api/backend. There is no override.
"""
import json, sys, urllib.request, urllib.error, collections

def get(base, path):
    try:
        with urllib.request.urlopen(f"http://{base}{path}", timeout=8) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, None
    except Exception as e:                                   # noqa: BLE001
        return 0, {"_error": str(e)}

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    base = sys.argv[1]
    st, body = get(base, "/api/backend")
    if st != 200 or not isinstance(body, dict) or body.get("simulated") is not True:
        sys.exit(f"REFUSING {base}: /api/backend did not prove simulated:true ({st} {body})")

    routes = [l.strip() for l in open("reference/routes-csharp.txt") if l.strip()]
    have, missing = [], []
    for r in routes:
        if not r.startswith("/api/"):
            continue
        status, b = get(base, r)
        # A route that reports itself unavailable is IMPLEMENTED and honest.
        if status == 200:
            have.append(r)
        else:
            missing.append(r)

    print(f"implemented: {len(have)}/{len(have)+len(missing)}")
    groups = collections.defaultdict(list)
    for r in missing:
        groups["/".join(r.split("/")[:3])].append(r)
    print(f"\nnot implemented ({len(missing)}), by group:")
    for g in sorted(groups, key=lambda k: -len(groups[k])):
        print(f"  {g:24s} {len(groups[g]):3d}  {', '.join(x.split('/')[-1] for x in sorted(groups[g]))[:70]}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
