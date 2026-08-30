#!/bin/sh
# Install a freshly built host binary over the running service, and PROVE the
# service came back on the new one.
#
# ⚠️ This exists because it went wrong. sync.sh builds on the VM but installs
# nothing, so the systemd service kept running a binary from hours earlier while
# new routes were being "verified" against it - the routes 404'd, and the only
# reason it was caught was that the 404 was obvious. A silent behaviour change
# would not have been.
#
# The check at the end is the point: it compares the build id the running
# service reports against the binary just installed. A restart that silently
# failed over to the old unit, or an install that did not land, fails here.
set -e
: "${HAMDECK_BUILD_HOST:?set HAMDECK_BUILD_HOST to an ssh target (see SITE.md)}"
: "${HAMDECK_INSTALL_PATH:=/opt/hamdeck-cpp/hamdeck-host}"
: "${HAMDECK_SERVICE:=hamdeck-cpp.service}"
: "${HAMDECK_API:=http://127.0.0.1:5001}"

ssh "$HAMDECK_BUILD_HOST" "
set -e
BUILT=\$HOME/hamdeck-cpp/build/hamdeck-host
test -x \"\$BUILT\" || { echo 'no built binary - run sync.sh first' >&2; exit 1; }
# ⚠️ The SAME hash /api/build reports (FNV-1a over the file, first 12 hex).
# A different hash function here would never match and the check would be a
# permanent false alarm - which is the same as no check.
WANT=\$(python3 -c \"
import sys
h=1469598103934665603
with open(sys.argv[1],'rb') as f:
    for b in iter(lambda: f.read(65536), b''):
        for c in b:
            h^=c; h=(h*1099511628211)%2**64
print('%016x'%h)
\" \"\$BUILT\" | cut -c1-12)

# ⚠️ Refuse to install a binary whose own tests have not been run here. A green
# build is not a green suite.
( cd \$HOME/hamdeck-cpp/build && ctest --output-on-failure >/dev/null ) \
  || { echo 'TESTS FAILED - not installing' >&2; exit 1; }

sudo install -m755 \"\$BUILT\" '$HAMDECK_INSTALL_PATH'
sudo systemctl restart '$HAMDECK_SERVICE'

# Give it a moment, then confirm the SERVICE is running the binary we installed.
for i in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  systemctl is-active --quiet '$HAMDECK_SERVICE' && break
done
systemctl is-active --quiet '$HAMDECK_SERVICE' || {
  echo 'service did not come back:' >&2
  journalctl -u '$HAMDECK_SERVICE' -n 20 --no-pager >&2
  exit 1
}
GOT=\$(curl -s --max-time 5 '$HAMDECK_API/api/build' \
        | sed -n 's/.*\"build\":\"\([^\"]*\)\".*/\1/p')
if [ \"\$GOT\" != \"\$WANT\" ]; then
  echo \"MISMATCH: service reports build '\$GOT', installed '\$WANT'\" >&2
  exit 1
fi
echo \"deployed: \$WANT (service running the binary just installed)\"
"
