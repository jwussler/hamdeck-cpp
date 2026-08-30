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
# ⚠️ QSettings reads XDG_CONFIG_HOME IN PREFERENCE TO HOME. Setting HOME alone
# works on a box where XDG_CONFIG_HOME is unset and does nothing on a GitHub
# runner, where it is set - so the saved-geometry cases below silently ran the
# FRESH path and reported ok. Set both.
export HOME=/tmp/placement-home
export XDG_CONFIG_HOME=$HOME/.config
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

# ⚠️ `xwininfo -id <client> -frame` RETURNS THE CLIENT RECTANGLE. It reads like
# a frame measurement and is not one, so the first version of this script never
# looked at the decoration at all - the exact thing it was written to check.
# _NET_FRAME_EXTENTS is the window manager's own statement of how far the
# decoration extends past the client area on each side.
CLIENT=$(DISPLAY=$DISP xwininfo -id $WID | awk '
  /Absolute upper-left X/ {x=$4}
  /Absolute upper-left Y/ {y=$4}
  /Width:/  {w=$2}
  /Height:/ {h=$2}
  END {print x" "y" "w" "h}')
read CX CY CW CH <<< "$CLIENT"

EXT=$(DISPLAY=$DISP xprop -id $WID _NET_FRAME_EXTENTS 2>/dev/null |
      sed 's/.*= //; s/,//g')
kill $APP 2>/dev/null; sleep 1; kill $OB $XVFB 2>/dev/null

# No extents means no measurement. Fail rather than report ok on a number that
# was never taken.
if [ -z "$EXT" ] || [ "$EXT" = "not found." ]; then
  printf "  %-9s %-16s FAIL: no _NET_FRAME_EXTENTS - the decoration was never measured\n" \
         "$SCREEN" "$CASE"
  exit 1
fi
read EL ER ET EB <<< "$EXT"
FX=$((CX - EL)); FY=$((CY - ET))
FW=$((CW + EL + ER)); FH=$((CH + ET + EB))

VERDICT=ok
# ⚠️ The saved cases assert the SAVED SIZE came back. Without this, a settings
# file the app never read leaves the fresh default on screen, fully visible, and
# the case passes while testing nothing - which is what it did in CI.
case "$CASE" in
  # The WIDTH is the tell: 900 is the saved value and no default produces it
  # (a fresh window is 880 x the scale). The HEIGHT is legitimately clamped on
  # a screen too short for it, so it is checked as a bound, not an equality -
  # asserting 800 here failed a 1024x600 screen for behaving correctly.
  saved-*) { [ "$CW" -eq 900 ] && [ "$CH" -le 800 ]; } || \
             VERDICT="FAIL saved geometry was not read (got ${CW}x${CH}, wanted 900 wide)";;
esac
[ "$FY" -lt 0 ] && VERDICT="FAIL title bar above the screen"
[ "$FX" -lt 0 ] && VERDICT="FAIL frame left of the screen"
[ $((FX+FW)) -gt "$W" ] && VERDICT="FAIL frame past the right edge"
[ $((FY+FH)) -gt "$H" ] && VERDICT="FAIL frame below the bottom edge"
printf "  %-9s %-16s client %4d,%-4d %4dx%-4d  frame %4d,%-4d %4dx%-4d  deco %s  %s\n" \
       "$SCREEN" "$CASE" "$CX" "$CY" "$CW" "$CH" "$FX" "$FY" "$FW" "$FH" "$ET" "$VERDICT"
[ "$VERDICT" = ok ]
