#!/usr/bin/env python3
"""A stand-in TG-XL, so the tune sequence can be tested without keying a radio.

⚠️ IT REPRODUCES THE CONNECT BURST, WHICH IS THE WHOLE POINT. The real tuner
emits status lines the instant you connect - tuning=0, tuning=1, tuning=0 -
within milliseconds, long before any tuning happens. A client that treats the
first 1->0 as "tuned" reports a completed tune in a few milliseconds and unkeys
before the tuner has done anything. This fake sends that burst deliberately, so
a client that gets it wrong FAILS here rather than on the air.

Then it behaves like the real one: after C1|autotune it reports tuning=1 for a
few seconds, then tuning=0 for good.

    tools/fake_tgxl.py [--port 9010] [--tune-seconds 4] [--never-start]

--never-start answers autotune but never reports tuning=1, which is the case
where the client must give up and unkey rather than hold the carrier.
"""

import argparse
import socket
import threading
import time

BANNER = b"V1.2.17\n"


def status(tuning: int, freq="14200.000") -> bytes:
    return (f"S1|status fwd=22.24 peak=22.24 max=62.43 swr=1.2 pttA=1 bandA=6 "
            f"modeA=3 freqA={freq} tuning={tuning}\n").encode()


def serve_one(conn: socket.socket, tune_seconds: float, never_start: bool) -> None:
    conn.sendall(BANNER)
    # The burst: 0, 1, 0 inside a few milliseconds. Not a tune.
    conn.sendall(status(0)); time.sleep(0.01)
    conn.sendall(status(1)); time.sleep(0.01)
    conn.sendall(status(0))

    started_at = None
    conn.settimeout(0.5)
    buf = b""
    while True:
        try:
            data = conn.recv(1024)
            if not data:
                return
            buf += data
        except socket.timeout:
            data = b""
        except OSError:
            return

        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if b"autotune" in line and started_at is None and not never_start:
                started_at = time.monotonic()

        if started_at is None:
            conn.sendall(status(0))
        elif time.monotonic() - started_at < tune_seconds:
            conn.sendall(status(1))
        else:
            conn.sendall(status(0))
        time.sleep(0.2)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9010)
    ap.add_argument("--tune-seconds", type=float, default=4.0)
    ap.add_argument("--never-start", action="store_true")
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(4)
    print(f"fake TG-XL on 127.0.0.1:{args.port} "
          f"({'never starts' if args.never_start else f'{args.tune_seconds}s tune'})",
          flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve_one,
                         args=(conn, args.tune_seconds, args.never_start),
                         daemon=True).start()


if __name__ == "__main__":
    main()
