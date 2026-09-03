#!/usr/bin/env python3
# check_dead_link.py - the PHONE case, which the /ws/tx close handler cannot see.
#
# ⚠️ The socket is deliberately left OPEN. A handset that walks into a tunnel or
# is suspended by iOS sends no FIN: the connection stops carrying data while
# still looking healthy from the host, so the close callback never runs. The only
# signal is the GAP in the audio stream, and this is the test for it.
import json, os, sys, time, urllib.request, http.cookiejar, websocket

BASE = os.environ.get("HAMDECK_BASE", "http://127.0.0.1:5902")
USER = os.environ.get("HAMDECK_USER", "joe")
PASS = os.environ.get("HAMDECK_PASS", "testpw123")
GAP  = float(os.environ.get("HAMDECK_GAP_S", "8"))   # must exceed tx_link_timeout_ms

cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
def get(p): return json.load(op.open(BASE + p))

# ⚠️ Prove the instrument can see the host first. A dead host and a rig that
# stayed keyed are the same silence from here.
try:
    urllib.request.urlopen(BASE + "/api/health", timeout=3).read()
except Exception as e:
    print(f"   NO HOST on {BASE} - {e}"); sys.exit(2)

op.open(urllib.request.Request(BASE + "/api/auth/login",
        json.dumps({"username": USER, "password": PASS}).encode(),
        {"Content-Type": "application/json"}))
cookie = "; ".join(f"{c.name}={c.value}" for c in cj)

ws = websocket.create_connection(BASE.replace("http", "ws") + "/ws/tx",
                                 header=[f"Cookie: {cookie}"])
silence = b"\x00\x00" * 441          # one 20 ms frame at 22050, shape only
for _ in range(10):                   # arm the check: frames have been arriving
    ws.send_binary(silence); time.sleep(0.02)

get("/api/ptt/on"); time.sleep(0.5)
for _ in range(15):                   # a normal over: keyed, streaming
    ws.send_binary(silence); time.sleep(0.02)
keyed = get("/api/status")["tx"]
print(f"   keyed and streaming      : tx={keyed}")

# ── the tunnel. Socket stays open; nothing is sent. ──────────────────────────
print(f"   ...link goes quiet, socket still open, waiting {GAP:.0f}s")
time.sleep(GAP)
after = get("/api/status")["tx"]
print(f"   after the silent gap     : tx={after}")

try: ws.close()
except Exception: pass

if not keyed:
    print("   INCONCLUSIVE - never keyed"); sys.exit(2)
print("   PASS - a silent link unkeyed the rig" if not after
      else "   FAIL - THE RIG IS STILL KEYED (dead link went unnoticed)")
sys.exit(0 if not after else 1)
