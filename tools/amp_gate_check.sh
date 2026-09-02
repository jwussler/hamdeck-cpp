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
    {"username":"deckop",  "password_hash":"$HASH","is_admin":false,"can_transmit":true,"is_station":false},
    {"username":"cfgop",   "password_hash":"$HASH","is_admin":false,"can_transmit":true,"is_station":true}
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

say "0. the right arrives from the CONFIG FILE, not only from an admin call"
# â ï¸ THE CHECK THAT WAS MISSING, and its absence shipped a broken host.
# Every other step here grants the right through the admin API, which exercises
# SetIsStation on a RUNNING host. The other way in - config -> AuthService at
# startup - was never touched, and main.cpp did not pass is_station to AddUser at
# all. A default argument of `false` made that compile silently, so the file said
# the operator had the right and the running host said they did not.
# Two mechanisms are two tests. cfgop gets it from the file and nothing else.
CFG_TOK=$(login cfgop)
b=$(curl -s "http://127.0.0.1:$DASH/api/tune/amp/probe?token=$CFG_TOK")
c=$(code "http://127.0.0.1:$DASH/api/tune/amp/probe?token=$CFG_TOK")
[ "$c" != "403" ] && ok "config-declared station right reached the running host" \
                  || bad "is_station in the config did not load (HTTP $c): $b"
# and /api/auth/status must agree, since that is what a client greys the button on
printf '%s' "$(curl -s "http://127.0.0.1:$DASH/api/auth/status?token=$CFG_TOK")" \
  | grep -q '"is_station":true' \
  && ok "/api/auth/status reports it too" \
  || bad "/api/auth/status does not report is_station for a station account"

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

say "3b. station right is NOT enough on its own - transmit must also be allowed"
# â ï¸ The live host has an account shaped exactly like this: the `pusher`
# account the Stream Deck session belongs to has can_transmit=false. Amp tune is
# gated separately from IsTransmitRoute, so without this check a station grant
# would hand a ten-second carrier to an account explicitly denied transmit.
curl -fsS "http://127.0.0.1:$DASH/api/admin/user/tx/disable/deckop?token=$BOSS" >/dev/null
c=$(code "http://127.0.0.1:$DASH/api/tune/amp?token=$DECK")
[ "$c" = "403" ] && ok "station right alone does not key the rig" \
                 || bad "an account denied transmit could amp tune, got $c"
curl -fsS "http://127.0.0.1:$DASH/api/admin/user/tx/enable/deckop?token=$BOSS" >/dev/null

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

say "5b. THE ACTUAL STREAM DECK URL - note the trailing slash"
# â ï¸ This is the bug the operator kept reporting. The deck button sends
# "/api/tune/amp/", which did not match the exact route and fell through to the
# prefix catch-all - answering 200 "Amp tuner is not configured" and never tuning.
# The reference host trims trailing slashes on /api/ paths (ApiServer.cs:766);
# this host did not. Measured from the live journal, not guessed:
#     dash GET /api/tune/amp/
b=$(curl -s "http://127.0.0.1:$CTRL/api/tune/amp/")
if printf '%s' "$b" | grep -q '"action":"started"\|"action":"stopped"'; then
  ok "trailing-slash URL reaches the real tuner: $b"
else
  bad "trailing slash did not reach the tuner: $b"
fi

say "6. the prefix guard agrees with the exact route"
c=$(code "http://127.0.0.1:$DASH/api/tune/amp/anything?token=$DECK")
[ "$c" = "403" ] && ok "/api/tune/amp/... refuses too" || bad "prefix guard disagrees, got $c"

[ "$FAIL" = "0" ] && say "PASS" || say "FAILED"
exit "$FAIL"
