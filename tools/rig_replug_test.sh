#!/bin/bash
# GATE: prove the station recovers by itself when the radio is unplugged and
# plugged back in.
#
# 09/02/2026 it did not. The cable moved, CAT came back as a different minor
# number, and the host sat there holding /dev/ttyUSB0 (deleted) reporting
# rig_connected:false until someone noticed.
#
# The replug is simulated by unbinding BOTH USB devices from the kernel's usb
# driver and binding them back. That produces the same udev remove/add events a
# physical replug does, which is what the recovery is built on. It does not
# touch the hypervisor's passthrough.
#
# Run it ON THE HOST BOX (VM 105).  sudo ./tools/rig_replug_test.sh
set -uo pipefail

HEALTH=http://127.0.0.1:5001/api/health
CAT_ID=10c4:ea70          # CP2105 dual UART  (CAT)
CODEC_ID=08bb:29c3        # PCM2903C          (audio)
DEADLINE=${DEADLINE:-90}

[ "$(id -u)" = 0 ] || { echo "FAIL: run with sudo"; exit 1; }

connected() { curl -s -m3 "$HEALTH" | grep -q '"rig_connected":true'; }

# The sysfs name (e.g. "2-1") of a usb device, by vendor:product.
usb_path() {
  local vid=${1%:*} pid=${1#*:} d
  for d in /sys/bus/usb/devices/*; do
    [ -r "$d/idVendor" ] || continue
    if [ "$(cat "$d/idVendor")" = "$vid" ] && [ "$(cat "$d/idProduct")" = "$pid" ]; then
      basename "$d"; return 0
    fi
  done
  return 1
}

cat_path=$(usb_path $CAT_ID)   || { echo "FAIL: CP2105 not present - nothing to test"; exit 1; }
codec_path=$(usb_path $CODEC_ID) || { echo "FAIL: codec not present - nothing to test"; exit 1; }
echo "CAT at $cat_path, codec at $codec_path"

connected || { echo "FAIL: rig is not connected BEFORE the test - fix that first"; exit 1; }
echo "before: rig_connected=true"

# Always try to put the radio back, even if the script dies mid-way.
#
# ⚠️ Test for the DRIVER symlink, not for the device directory. Unbinding does
# not remove the device from /sys/bus/usb/devices - it only detaches the driver -
# so a "does the device still exist" guard here skips the rebind every time and
# leaves the station off the air. That happened on the first run of this script.
rebind() {
  for p in "$codec_path" "$cat_path"; do
    [ -e "/sys/bus/usb/devices/$p/driver" ] && continue
    echo -n "$p" > /sys/bus/usb/drivers/usb/bind 2>/dev/null
    sleep 1
  done
}
trap rebind EXIT

echo -n "$cat_path"   > /sys/bus/usb/drivers/usb/unbind
echo -n "$codec_path" > /sys/bus/usb/drivers/usb/unbind
sleep 3
echo "unplugged: /dev/ttyRIG exists? $([ -e /dev/ttyRIG ] && echo yes || echo no); unit $(systemctl is-active hamdeck-cpp.service)"

rebind
trap - EXIT

start=$SECONDS
while [ $((SECONDS - start)) -lt $DEADLINE ]; do
  if connected; then
    took=$((SECONDS - start))
    pid=$(systemctl show -p MainPID --value hamdeck-cpp.service)
    stale=$(ls -l /proc/"$pid"/fd 2>/dev/null | grep -c "(deleted)")
    node=$(readlink -f /dev/ttyRIG)
    echo "after: rig_connected=true in ${took}s, CAT node $node, stale fds $stale"
    [ "$stale" = 0 ] || { echo "FAIL: recovered but still holding $stale dead handles"; exit 1; }
    # Connected is not the same as reading the rig. Ask it something.
    curl -s -m3 http://127.0.0.1:5001/api/status | grep -q '"freq":[1-9]' \
      || { echo "FAIL: rig_connected=true but /api/status has no frequency"; exit 1; }
    echo "PASS"
    exit 0
  fi
  sleep 2
done

echo "FAIL: still not connected ${DEADLINE}s after the radio came back"
systemctl is-active hamdeck-cpp.service
curl -s -m3 "$HEALTH"; echo
exit 1
