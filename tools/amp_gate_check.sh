#!/usr/bin/env bash
# Does the amp tune gate actually refuse, and actually open?
#
# ⚠️ THIS EXISTS BECAUSE THE ROUTE'S OWN REPLY PROVES NOTHING. The amp tune
# button was dead for weeks while every check looked healthy: the host answered
# HTTP 200 with an error body, and a Stream Deck button reads 200 as success. A
# gate that refuses with a success code is indistinguishable from one that works.
#
# So this drives the REAL binary over HTTP on both listeners and asserts the
# STATUS CODE, which is the thing the deck actually reacts to.
#
#   1  a session with no station right   -> 403
#   2  the same account, right granted   -> not 403
#   3  the loopback control listener     -> not 403, with no session at all
#   4  granting transmit does NOT grant station
#
# ⚠️ It refuses to run against anything but a simulator, the same fail-closed
# check tools/walk_all_routes.py makes, because step 2 can key a transmitter.
set -u

FAIL=0
say()  { printf '%s\n' "$*"; }
ok()   { printf '  ok   %s\n' "$*"; }
bad()  { printf '  FAIL %s\n' "$*"; FAIL=1; }

DASH=18502
CTRL=18501
DIR="$(mktemp -d)"
CFG="$DIR/config.json"
trap 'kill %1 2>/dev/null; rm -rf "$DIR"' EXIT

# ⚠️ Hash generated here, not pasted: the parameters must match src/auth.cpp and
# a stale copy in a test fails in a way that looks like a broken gate.
HASH=$(python3 - <<'PY'
import hashlib, os, binascii
salt = os.urandom(16)
h = hashlib.pbkdf2_hmac('sha256', b'gatecheck', salt, 350000, 32)
print("pbkdf2:%s:%s" % (binascii.hexlify(salt).decode(), binascii.hexlify(h).decode()))
PY
)

cat > "$CFG" <<JSON
{
  "radio_port": "",
  "api_port": $CTRL,
  "dashboard_port": $DASH,
  "web_users": [
    {"username":"boss",    "password_hash":"$HASH","is_admin":true, "can_transmit":true,"is_station":false},
    {"username":"deckop",  "password_hash":"$HASH","is_admin":false,"can_transmit":true,"is_station":false}
  ]
}
JSON

HAMDECK_CONFIG="$CFG" ./build/hamdeck-host >"$DIR/host.log" 2>&1 &
for _ in $(seq 1 50); do
  curl -fsS "http://127.0.0.1:$DASH/api/health" >/dev/null 2>&1 && break
  sleep 0.2
done

# ── Fail closed: only ever run this against the simulator ────────────────────
if ! curl -fsS "http://127.0.0.1:$CTRL/api/backend" 2>/dev/null | grep -q '"simulated":[[:space:]]*true'; then
  say "REFUSING: target did not prove it is a simulator (/api/backend simulated:true)"
  say "step 2 of this check can key a transmitter. There is deliberately no --force."
  exit 2
fi

login() {  # login <user> -> prints token
  curl -fsS -X POST "http://127.0.0.1:$DASH/api/auth/login" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"$1\",\"password\":\"gatecheck\"}" -D - -o /dev/null 2>/dev/null \
    | sed -n 's/.*hamdeck_session=\([^;]*\).*/\1/p' | tr -d '\r'
}
code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

DECK=$(login deckop)
BOSS=$(login boss)
[ -n "$DECK" ] && [ -n "$BOSS" ] || { say "could not log in - check $DIR/host.log"; exit 2; }

say "1. no station right"
c=$(code "http://127.0.0.1:$DASH/api/tune/amp?token=$DECK")
[ "$c" = "403" ] && ok "refused with 403 (not a 200 the deck would read as success)" \
                 || bad "expected 403, got $c"

say "2. transmit rights do NOT imply station rights"
# deckop already has can_transmit=true and must still be refused above.
c=$(code "http://127.0.0.1:$DASH/api/ptt/off?token=$DECK")
[ "$c" = "200" ] && ok "the same account CAN transmit, and still cannot amp tune" \
                 || bad "expected the transmit route to work for this account, got $c"

say "3. station right granted"
# â ï¸ NOT "is it non-403". The bug being guarded against ANSWERS 200 WITH A
# REFUSAL, so a status-only assertion here passes while the gate is broken -
# which is exactly what happened the first time this check was run against an
# injected bug. The body has to show the amp route actually ran.
curl -fsS "http://127.0.0.1:$DASH/api/admin/user/station/enable/deckop?token=$BOSS" >/dev/null
b=$(curl -s "http://127.0.0.1:$DASH/api/tune/amp?token=$DECK")
c=$(code "http://127.0.0.1:$DASH/api/tune/amp?token=$DECK")
if [ "$c" = "200" ] && printf '%s' "$b" | grep -q '"tuner":"amp"' \
   && ! printf '%s' "$b" | grep -qi 'station right\|connected locally'; then
  ok "allowed, and the reply came from the amp route: $b"
else
  bad "expected the amp route to answer, got HTTP $c body: $b"
fi

say "4. revoking reaches the LIVE session, not just the stored user"
curl -fsS "http://127.0.0.1:$DASH/api/admin/user/station/disable/deckop?token=$BOSS" >/dev/null
c=$(code "http://127.0.0.1:$DASH/api/tune/amp?token=$DECK")
[ "$c" = "403" ] && ok "refused again without a re-login" \
                 || bad "revoke did not reach the live session, got $c"

say "5. the loopback control listener still needs no session"
b=$(curl -s "http://127.0.0.1:$CTRL/api/tune/amp")
c=$(code "http://127.0.0.1:$CTRL/api/tune/amp")
if [ "$c" = "200" ] && printf '%s' "$b" | grep -q '"tuner":"amp"'; then
  ok "local console unchanged: $b"
else
  bad "control listener did not reach the amp route, HTTP $c body: $b"
fi

say "6. the prefix guard agrees with the exact route"
c=$(code "http://127.0.0.1:$DASH/api/tune/amp/anything?token=$DECK")
[ "$c" = "403" ] && ok "/api/tune/amp/... refuses too" || bad "prefix guard disagrees, got $c"

[ "$FAIL" = "0" ] && say "PASS" || say "FAILED"
exit "$FAIL"
