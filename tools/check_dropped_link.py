#!/usr/bin/env python3
# check_dropped_link.py - proves the /ws/tx CLOSE path unkeys the rig: key it, drop the socket like a
# phone losing signal, and read the rig back through a route that is NOT the one
# under test.
import json, sys, time, urllib.request, http.cookiejar, websocket

import os
BASE = os.environ.get("HAMDECK_BASE", "http://127.0.0.1:5902")
USER = os.environ.get("HAMDECK_USER", "joe")
PASS = os.environ.get("HAMDECK_PASS", "testpw123")
cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

def post(path, obj):
    r = urllib.request.Request(BASE+path, json.dumps(obj).encode(),
                               {"Content-Type": "application/json"})
    return json.load(op.open(r))
def get(path):
    return json.load(op.open(BASE+path))

# ⚠️ PROVE THE INSTRUMENT CAN SEE THE HOST FIRST. A dead host and a rig that
# stayed keyed are the same silence from here, and reporting the second when it
# was the first is a false finding.
try:
    urllib.request.urlopen(BASE+"/api/health", timeout=3).read()
except Exception as e:
    print(f"   NO HOST on {BASE} - {e}"); sys.exit(2)

post("/api/auth/login", {"username": USER, "password": PASS})
cookie = "; ".join(f"{c.name}={c.value}" for c in cj)

ws = websocket.create_connection("ws://127.0.0.1:5902/ws/tx",
                                 header=[f"Cookie: {cookie}"])
get("/api/ptt/on"); time.sleep(0.4)
keyed = get("/api/status")["tx"]
print(f"   keyed with the socket up : tx={keyed}")

ws.close()                      # the dropped link
time.sleep(1.0)
after = get("/api/status")["tx"]
print(f"   after the socket dropped : tx={after}")

if not keyed:
    print("   INCONCLUSIVE - never keyed"); sys.exit(2)
print("   PASS - the dropped link unkeyed the rig" if not after
      else "   FAIL - THE RIG IS STILL KEYED")
sys.exit(0 if not after else 1)
