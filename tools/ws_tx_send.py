#!/usr/bin/env python3
"""Send known PCM into a host's /ws/tx and report what the host did with it.

⚠️ THIS EXISTS BECAUSE THE HOST'S TRANSMIT PATH HAD NEVER RUN. After a live
keyup the station host reported tx_accepted:0, tx_dropped:0 - the client had a
bug that meant no audio ever arrived, and as a side effect accept -> queue ->
pump -> codec had never once been exercised on real hardware. A path nobody has
run is not a working path; it is an untested one that happens to compile.

Speaks WebSocket directly - no dependency to install on a station box. Only what
RFC 6455 needs for this job: a handshake, text frames in, masked binary out.

⚠️ POINT THIS AT A SIMULATED HOST. The host only accepts audio while the RIG
reports keyed, so against a real station this would need a real transmitter
keyed - it would put this tone on the air. --require-simulated is the default
and refuses to run unless /api/backend says simulated:true.
"""

import argparse
import base64
import json
import math
import os
import socket
import struct
import sys
import time
import urllib.request


def handshake(sock, host, port, path):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("server closed during handshake")
        buf += chunk
    head, rest = buf.split(b"\r\n\r\n", 1)
    if b"101" not in head.split(b"\r\n")[0]:
        raise RuntimeError("upgrade refused: " + head.split(b"\r\n")[0].decode())
    return rest


def read_frame(sock, pending):
    """Return (opcode, payload, leftover). Blocks. Server frames are unmasked."""

    def need(n, buf):
        while len(buf) < n:
            chunk = sock.recv(65536)
            if not chunk:
                raise RuntimeError("server closed")
            buf += chunk
        return buf

    pending = need(2, pending)
    b0, b1 = pending[0], pending[1]
    opcode = b0 & 0x0F
    ln = b1 & 0x7F
    off = 2
    if ln == 126:
        pending = need(4, pending)
        ln = struct.unpack(">H", pending[2:4])[0]
        off = 4
    elif ln == 127:
        pending = need(10, pending)
        ln = struct.unpack(">Q", pending[2:10])[0]
        off = 10
    pending = need(off + ln, pending)
    return opcode, pending[off:off + ln], pending[off + ln:]


def send_binary(sock, payload):
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    n = len(payload)
    header = b"\x82"
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        header += bytes([0x80 | 127]) + struct.pack(">Q", n)
    sock.sendall(header + mask + masked)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5011)
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--tone", type=float, default=700.0)
    ap.add_argument("--amplitude", type=int, default=8000)
    ap.add_argument("--allow-real-radio", action="store_true",
                    help="DANGER: permit running against a non-simulated host. "
                         "That means a real transmitter and this tone on the air.")
    args = ap.parse_args()

    base = f"http://{args.host}:{args.port}"
    with urllib.request.urlopen(base + "/api/backend", timeout=5) as r:
        backend = json.load(r)
    print(f"backend: {json.dumps(backend)}")

    # ⚠️ Refuses by default rather than trusting the operator to have pointed it
    # somewhere safe. The reference tooling learned this the same way.
    if not backend.get("simulated") and not args.allow_real_radio:
        print("REFUSING: host is not simulated. This would key a real "
              "transmitter. Pass --allow-real-radio only if that is intended.")
        return 2

    before_accepted = backend.get("tx_accepted", 0)
    before_dropped = backend.get("tx_dropped", 0)

    sock = socket.create_connection((args.host, args.port), timeout=10)
    pending = handshake(sock, args.host, args.port, "/ws/tx")

    # The host sends its config - or an error - the moment the socket opens.
    sock.settimeout(5)
    opcode, payload, pending = read_frame(sock, pending)
    msg = json.loads(payload.decode())
    print(f"host said: {msg}")
    if msg.get("type") == "error":
        print("REFUSED by host: " + msg.get("message", ""))
        return 1
    rate = int(msg.get("sample_rate", 48000))

    frames_per_chunk = 960                      # 20 ms at 48 k, the host's chunk
    chunks = int(args.seconds * rate / frames_per_chunk)
    phase = 0.0
    step = 2.0 * math.pi * args.tone / rate
    sent_bytes = 0

    # ⚠️ Paced in real time. Audio produced faster than real time fills the
    # host's queue and every later frame arrives late - which is a different
    # failure from the one being tested, and it would look like this one.
    t0 = time.monotonic()
    for c in range(chunks):
        buf = bytearray()
        for _ in range(frames_per_chunk):
            buf += struct.pack("<h", int(args.amplitude * math.sin(phase)))
            phase += step
            if phase > 2.0 * math.pi:
                phase -= 2.0 * math.pi
        send_binary(sock, bytes(buf))
        sent_bytes += len(buf)
        target = t0 + (c + 1) * frames_per_chunk / rate
        delay = target - time.monotonic()
        if delay > 0:
            time.sleep(delay)

    sock.close()
    time.sleep(0.5)

    with urllib.request.urlopen(base + "/api/backend", timeout=5) as r:
        after = json.load(r)
    print(f"sent {sent_bytes} bytes ({sent_bytes // 2} frames) in "
          f"{time.monotonic() - t0:.2f}s")
    print(f"accepted {before_accepted} -> {after.get('tx_accepted')}   "
          f"dropped {before_dropped} -> {after.get('tx_dropped')}   "
          f"queue {after.get('tx_queue')}   "
          f"device_queued_ms {after.get('device_queued_ms')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
