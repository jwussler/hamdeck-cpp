#!/bin/bash
# Where does the window actually LAND, decoration and all?
#
# Measures the client's window under a REAL window manager (openbox under Xvfb)
# and reads the frame back with `xwininfo -frame` - the thing the operator has
# to grab.
#
# ⚠️ THE REASON THIS EXISTS: 0.1.2 shipped a window nobody could move. Every
# check before it ran under the offscreen QPA, which has no window manager and
# no decoration, so a title bar placed above the top of the screen was invisible
# by construction. x/y position the CLIENT AREA, so y=0 puts the decoration off
# the display.
#
# The arithmetic has its own test, with no window in it: ctest -R place.
#
# Needs: xvfb, openbox, x11-utils, xdotool.
# Set HAMDECK_CLIENT to point at a binary elsewhere.
#
# Usage: tools/placement_check.sh <screenWxH> <case>
#        case = fresh | saved-at-origin | saved-offscreen
set -u
SCREEN=$1; CASE=$2
CLIENT=${HAMDECK_CLIENT:-$(dirname "$0")/../client/build/hamdeck-qml}
W=${SCREEN%x*}; H=${SCREEN#*x}
DISP=:97
export HOME=/tmp/placement-home
rm -rf $HOME; mkdir -p $HOME/.config/HamDeck

if [ "$CASE" = "saved-offscreen" ]; then
  # A position from a monitor that is no longer plugged in.
  cat > $HOME/.config/HamDeck/HamDeckClient.ini <<INI
[General]
window_geometry=@Rect(3200 400 900 800)
INI
fi
if [ "$CASE" = "saved-at-origin" ]; then
  # What an older build wrote, and what an upgrade must not honour blindly.
  cat > $HOME/.config/HamDeck/HamDeckClient.ini <<INI
[General]
window_geometry=@Rect(0 0 900 800)
INI
fi

Xvfb $DISP -screen 0 ${W}x${H}x24 >/dev/null 2>&1 &
XVFB=$!
sleep 2
DISPLAY=$DISP openbox --sm-disable >/dev/null 2>&1 &
OB=$!
sleep 2

DISPLAY=$DISP "$CLIENT" >/dev/null 2>&1 &
APP=$!
sleep 5

WID=$(DISPLAY=$DISP xdotool search --name "^HamDeck$" | head -1)
if [ -z "$WID" ]; then echo "$SCREEN $CASE: FAIL no window found"; kill $APP $OB $XVFB 2>/dev/null; exit 1; fi

# -frame includes the decoration the window manager drew: this is what the
# operator actually has to grab.
EVAL=$(DISPLAY=$DISP xwininfo -id $WID -frame | awk '
  /Absolute upper-left X/ {x=$4}
  /Absolute upper-left Y/ {y=$4}
  /Width:/  {w=$2}
  /Height:/ {h=$2}
  END {print x" "y" "w" "h}')
read FX FY FW FH <<< "$EVAL"
kill $APP 2>/dev/null; sleep 1; kill $OB $XVFB 2>/dev/null

VERDICT=ok
[ "$FY" -lt 0 ] && VERDICT="FAIL title bar above the screen"
[ "$FX" -lt 0 ] && VERDICT="FAIL frame left of the screen"
[ $((FX+FW)) -gt "$W" ] && VERDICT="FAIL frame past the right edge"
[ $((FY+FH)) -gt "$H" ] && VERDICT="FAIL frame below the bottom edge"
printf "  %-9s %-16s frame %5d,%-5d %4dx%-4d   %s\n" "$SCREEN" "$CASE" "$FX" "$FY" "$FW" "$FH" "$VERDICT"
[ "$VERDICT" = ok ]
