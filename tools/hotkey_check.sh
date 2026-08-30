#!/bin/bash
# Does the PTT hotkey actually key the rig?
#
# ⚠️ NOTHING TESTED THIS PATH. tests_hotkey.cpp covers PttHotkey's state machine
# - hold, toggle, auto-repeat suppression, focus loss - and it passes. What it
# cannot see is whether a key press in the RUNNING APPLICATION ever reaches that
# state machine, because that depends on which QML item holds focus. A unit test
# of the mechanism plus a UI that never delivers events to it is two green
# lights and a dead hotkey.
#
# ⚠️ RUNS AGAINST A SIMULATED HOST, NEVER THE STATION. It asserts by reading the
# RIG's tx state back out of /api/status - the same rule the route walker
# follows, because a client that thinks it keyed proves nothing - and keying a
# real transmitter to test a keyboard shortcut is not acceptable.
#
# Usage: HAMDECK_SIM_PORT=5012 HAMDECK_SIM_USER=... HAMDECK_SIM_PASS=... \
#        tools/hotkey_check.sh [hold|toggle]
set -u
MODE=${1:-hold}
PORT=${HAMDECK_SIM_PORT:?set HAMDECK_SIM_PORT (a SIMULATED host, not the station)}
USER_=${HAMDECK_SIM_USER:?}
PASS=${HAMDECK_SIM_PASS:?}
CLIENT=${HAMDECK_CLIENT:-$(dirname "$0")/../client/build/hamdeck-qml}
DISP=:96

# Refuse a host that is not simulated. Same guard as the route walker: the
# check must be structural, not something the operator has to remember.
BACKEND=$(curl -s --max-time 4 -X POST "http://127.0.0.1:$PORT/api/auth/login" \
          -H 'Content-Type: application/json' \
          -d "{\"username\":\"$USER_\",\"password\":\"$PASS\"}" -c /tmp/hk.jar >/dev/null;
          curl -s --max-time 4 -b /tmp/hk.jar "http://127.0.0.1:$PORT/api/backend")
case "$BACKEND" in
  *'"simulated":true'*) ;;
  *) echo "REFUSING: $PORT does not report simulated:true - this keys a transmitter"; exit 2;;
esac

export HOME=/tmp/hotkey-home
export XDG_CONFIG_HOME=$HOME/.config
rm -rf $HOME; mkdir -p $HOME/.config/HamDeck
# ptt_hold=false selects TOGGLE. Qt::Key_Pause is 0x01000017 = 16777239.
cat > $HOME/.config/HamDeck/HamDeckClient.ini <<INI
[General]
ptt_key=16777239
ptt_hold=$([ "$MODE" = hold ] && echo true || echo false)
INI

Xvfb $DISP -screen 0 1280x900x24 >/dev/null 2>&1 & XVFB=$!
sleep 2
DISPLAY=$DISP openbox --sm-disable >/dev/null 2>&1 & OB=$!
sleep 2
DISPLAY=$DISP "$CLIENT" --host 127.0.0.1 --port "$PORT" \
    --user "$USER_" --password "$PASS" >/tmp/hotkey-client.log 2>&1 & APP=$!
sleep 6

tx() { curl -s --max-time 4 -b /tmp/hk.jar "http://127.0.0.1:$PORT/api/status" |
       grep -o '"tx":[a-z]*' | cut -d: -f2; }

WID=$(DISPLAY=$DISP xdotool search --name "^HamDeck$" | head -1)
if [ -z "$WID" ]; then echo "FAIL: no window"; kill $APP $OB $XVFB 2>/dev/null; exit 1; fi
DISPLAY=$DISP xdotool windowactivate --sync $WID 2>/dev/null
sleep 1

FAILURES=0
say() { printf "  %-34s %s\n" "$1" "$2"; }
check() { [ "$2" = "$3" ] || { FAILURES=$((FAILURES+1)); say "$1" "FAIL: tx=$2, wanted $3"; return; }; say "$1" "ok (tx=$2)"; }

check "before pressing anything" "$(tx)" "false"

if [ "$MODE" = hold ]; then
  DISPLAY=$DISP xdotool keydown Pause; sleep 2
  check "key held" "$(tx)" "true"
  DISPLAY=$DISP xdotool keyup Pause; sleep 2
  check "key released" "$(tx)" "false"
else
  DISPLAY=$DISP xdotool key Pause; sleep 2
  check "first press (toggle on)" "$(tx)" "true"
  DISPLAY=$DISP xdotool key Pause; sleep 2
  check "second press (toggle off)" "$(tx)" "false"
fi

# ⚠️ The case that matters in real use: the operator has clicked something -
# a knob, a dropdown, a key on the panel - before reaching for the hotkey.
DISPLAY=$DISP xdotool mousemove --window $WID 640 700 click 1 2>/dev/null; sleep 1
if [ "$MODE" = hold ]; then
  DISPLAY=$DISP xdotool keydown Pause; sleep 2
  check "after clicking in the panel" "$(tx)" "true"
  DISPLAY=$DISP xdotool keyup Pause; sleep 2
else
  DISPLAY=$DISP xdotool key Pause; sleep 2
  check "after clicking in the panel" "$(tx)" "true"
  DISPLAY=$DISP xdotool key Pause; sleep 2
fi

# Leave nothing keyed, whatever happened above.
curl -s --max-time 4 -b /tmp/hk.jar "http://127.0.0.1:$PORT/api/ptt/off" >/dev/null
kill $APP 2>/dev/null; sleep 1; kill $OB $XVFB 2>/dev/null
[ "$FAILURES" -eq 0 ] && echo "HOTKEY ($MODE) PASSED" || echo "HOTKEY ($MODE) FAILED"
exit $([ "$FAILURES" -eq 0 ] && echo 0 || echo 1)
