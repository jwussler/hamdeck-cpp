#!/bin/bash
# Prove the HOST's transmit path carries audio, end to end, without a radio and
# without a Windows client.
#
# ⚠️ WRITTEN AFTER THE PATH TURNED OUT NEVER TO HAVE RUN. A live keyup produced
# tx_accepted:0 - the client had a bug that meant no audio ever arrived, so
# accept -> queue -> pump -> codec had never executed once on real hardware while
# everybody assumed it worked. Counting arrivals is not proof; this RECORDS what
# the host writes and measures it.
#
# The trick is snd-aloop standing in for the codec: the host plays into one end
# of the loopback and arecord captures the other, so the bytes that would have
# reached the radio can be examined instead.
#
# ⚠️ SIMULATED RIG ONLY. It never opens the CAT port or the real codec, so it is
# safe to run on the station box - but check the ports below are free first, and
# do not leave the instance running. Stray test hosts have been found on that box
# with four-hour uptimes.
#
# Usage:  tools/tx_path_test.sh [seconds]
set -u

SECS="${1:-3}"
DIR="${HAMDECK_TXTEST_DIR:-$HOME/hd-txtest}"
API=5011
DASH=5012
TONE=700
AMP=8000
HOST_BIN="${HAMDECK_HOST_BIN:-./build/hamdeck-host}"

fail() { echo "FAIL: $*"; exit 1; }

[ -x "$HOST_BIN" ] || fail "no host binary at $HOST_BIN (build it first)"
command -v arecord >/dev/null || fail "arecord not installed"

# ⚠️ The loopback is what makes this measurable. Without it there is nowhere for
# the audio to go that can also be read back.
if ! grep -q Loopback /proc/asound/cards; then
  sudo modprobe snd-aloop pcm_substreams=2 || fail "could not load snd-aloop"
  sleep 1
fi

if ss -lnt 2>/dev/null | grep -q ":$API "; then
  fail "port $API is in use - a stray test host? check: ps -ef | grep hamdeck-host"
fi

mkdir -p "$DIR"
cat > "$DIR/config.json" <<EOF
{
  "radio_port": "",
  "record_sample_rate": 22050,
  "alsa_capture_device": "null",
  "alsa_playback_device": "hw:Loopback,0,0",
  "api_port": $API,
  "dashboard_port": $DASH,
  "allow_anonymous_status": false,
  "ptt_timeout_seconds": 180,
  "web_session_timeout": 480,
  "web_users": [],
  "tgxl_host": "",
  "kmtronic_host": ""
}
EOF

HAMDECK_CONFIG="$DIR/config.json" "$HOST_BIN" > "$DIR/host.log" 2>&1 &
HOST_PID=$!
# ⚠️ Always take the instance down, on every exit path. This script is the reason
# the box had stray hosts on it in the first place.
trap 'kill $HOST_PID 2>/dev/null; wait $HOST_PID 2>/dev/null' EXIT
sleep 4

kill -0 $HOST_PID 2>/dev/null || { cat "$DIR/host.log"; fail "host did not start"; }
BACKEND=$(curl -s --max-time 5 "http://127.0.0.1:$API/api/backend")
echo "$BACKEND" | grep -q '"simulated":true' || fail "host is not simulated - refusing"
echo "$BACKEND" | grep -q 'null sink' && fail "tx sink is the null sink - the loopback did not open, nothing would be measured"
echo "backend: $BACKEND"

rm -f "$DIR/out.wav"
arecord -D hw:Loopback,1,0 -f S16_LE -r 48000 -c 1 -d $((SECS + 5)) "$DIR/out.wav" >/dev/null 2>&1 &
AR=$!
sleep 1

python3 "$(dirname "$0")/ws_tx_send.py" --port $API --seconds "$SECS" --tone $TONE --amplitude $AMP || fail "send failed"
wait $AR

# ⚠️ MEASURE THE AUDIO, do not count the bytes. A sink that wrote silence, or
# wrote at the wrong rate, produces a perfectly healthy byte count - which is
# exactly the kind of "working" reading this project has been caught by before.
python3 - "$DIR/out.wav" $TONE $AMP <<'PY'
import math, struct, sys, wave

path, tone, amp = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
w = wave.open(path)
rate = w.getframerate()
raw = w.readframes(w.getnframes())
s = struct.unpack("<%dh" % (len(raw) // 2), raw)

win = 4800
loud = [i for i in range(0, len(s) - win, win)
        if math.sqrt(sum(x * x for x in s[i:i + win]) / win) > 200]
if not loud:
    print("FAIL: the recording is SILENT - the host wrote nothing audible")
    sys.exit(1)

seg = s[loud[0]:loud[-1] + win]
rms = math.sqrt(sum(x * x for x in seg) / len(seg))
peak = max(abs(x) for x in seg)

def goertzel(x, f, sr):
    k = 2.0 * math.cos(2.0 * math.pi * f / sr)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2
        s2, s1 = s1, s0
    return math.sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) / len(x)

at_tone = goertzel(seg[:24000], tone, rate)
others = [goertzel(seg[:24000], f, rate) for f in (tone / 2, tone * 0.7, tone * 1.3, tone * 2)]
worst_other = max(others)

print(f"duration {len(seg)/rate:.2f}s  peak {peak}  rms {rms:.0f} "
      f"(pure sine at this amplitude: {amp/math.sqrt(2):.0f})")
print(f"energy at {tone:.0f} Hz: {at_tone:.1f}   loudest neighbour: {worst_other:.1f}   "
      f"ratio {at_tone/max(worst_other,0.01):.0f}:1")

ok = True
if peak < amp * 0.9 or peak > amp * 1.1:
    print(f"FAIL: peak {peak} is not the {amp} that was sent"); ok = False
if rms < amp / math.sqrt(2) * 0.9:
    print(f"FAIL: rms {rms:.0f} is too low - audio is being dropped or attenuated"); ok = False
if at_tone < worst_other * 20:
    print("FAIL: the tone is not cleanly dominant - wrong rate, or distortion"); ok = False
print("PASS: the host's transmit path carries audio intact" if ok else "FAILED")
sys.exit(0 if ok else 1)
PY
