#!/usr/bin/env python3
"""Who was on a recording?

Takes the .json sidecar a recording writes and answers it from the log, in two
layers that are NOT the same kind of claim:

  LOGGED    a QSO in the log whose time falls inside the recording. The
            callsign is a fact - the operator wrote it down.
  ON THE NET everyone checked in to the net that was running. These are
            CANDIDATES. A check-in says somebody was present, not that they
            were the voice on the tape.

⚠️ A LOOKUP FINDS MATCHES. IT NEVER PROVES AN ABSENCE. "Nothing logged in this
window" is not "nobody was there" - it is far more often a QSO that was never
logged, or a station heard and not worked. This tool says "nothing found" and
never "nobody", because a false negative printed as a finding is worse than no
answer at all.

⚠️ A LOGGED QSO IS AN INSTANT, NOT A SPAN. COL_TIME_OFF equals COL_TIME_ON on
every row in this log, so the timestamp is when the operator logged it - usually
the end of the exchange, sometimes minutes after. Hence --pad, and hence the
window is matched generously and reported with the offset shown, so a match at
the edge is visible as one rather than presented as a bullseye.

Connection comes from the environment, never from this file - the repo is not
where station details live:
    WAVELOG_DB_HOST WAVELOG_DB_USER WAVELOG_DB_PASS WAVELOG_DB_NAME
or  WAVELOG_DB_DOCKER=<container>  to shell into a local container instead.

Usage: identify_recording.py <sidecar.json> [more.json ...] [--pad SECONDS]
"""
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone

QSO_TABLE = "TABLE_HRD_CONTACTS_V01"

# ⚠️ MATCHING IS ON TIME ALONE, ON PURPOSE, AND THE BAND IS SHOWN SO A WRONG
# MATCH IS VISIBLE. Filtering by band would silently drop true matches whenever
# the sidecar's frequency is unreliable - the rig disconnected, or a QSY between
# the exchange and the log entry - and a dropped true match is invisible in a
# way a flagged odd one is not. So every QSO in the window is listed, and one on
# a different band is marked rather than hidden.
BANDS_HZ = [
    ("160m", 1_800_000, 2_000_000), ("80m", 3_500_000, 4_000_000),
    ("60m", 5_300_000, 5_450_000), ("40m", 7_000_000, 7_300_000),
    ("30m", 10_100_000, 10_150_000), ("20m", 14_000_000, 14_350_000),
    ("17m", 18_068_000, 18_168_000), ("15m", 21_000_000, 21_450_000),
    ("12m", 24_890_000, 24_990_000), ("10m", 28_000_000, 29_700_000),
    ("6m", 50_000_000, 54_000_000), ("2m", 144_000_000, 148_000_000),
]


def band_of(freq_hz):
    for name, lo, hi in BANDS_HZ:
        if lo <= freq_hz <= hi:
            return name
    return None

# The net name in a QSO comment, in the two encodings that coexist in this log -
# see qsl-queue's README, measured over 29,573 rows. Bracket text is NOT always
# a net ("[New call sign May 2025]" is in there), so a bracketed token is
# reported as the net only when it looks like one.
NET_BRACKET = re.compile(r"\[([^\]]+)\]\s*$")
NET_SHAPED = re.compile(r"\bnet\b", re.IGNORECASE)


def run_sql(sql):
    """Returns rows as lists of strings. Tab-separated, no header, NULL as \\N."""
    container = os.environ.get("WAVELOG_DB_DOCKER")
    if container:
        cmd = ["docker", "exec", container, "sh", "-lc",
               'mariadb -uroot -p"$MYSQL_ROOT_PASSWORD" -N -B '
               + os.environ.get("WAVELOG_DB_NAME", "wavelog")
               + " -e " + shell_quote(sql)]
    else:
        need = ("WAVELOG_DB_HOST", "WAVELOG_DB_USER", "WAVELOG_DB_PASS")
        missing = [k for k in need if not os.environ.get(k)]
        if missing:
            sys.exit("set " + ", ".join(missing) + " (or WAVELOG_DB_DOCKER)")
        cmd = ["mariadb", "-h", os.environ["WAVELOG_DB_HOST"],
               "-u", os.environ["WAVELOG_DB_USER"],
               "-p" + os.environ["WAVELOG_DB_PASS"], "-N", "-B",
               os.environ.get("WAVELOG_DB_NAME", "wavelog"), "-e", sql]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("database query failed: " + (out.stderr.strip() or "no message"))
    return [line.split("\t") for line in out.stdout.splitlines() if line]


def shell_quote(s):
    return "'" + s.replace("'", "'\\''") + "'"


def sql_str(s):
    return "'" + str(s).replace("\\", "\\\\").replace("'", "''") + "'"


def parse_utc(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def net_from_comment(comment):
    if not comment or comment == "\\N":
        return None
    m = NET_BRACKET.search(comment)
    if m:
        return m.group(1).strip() if NET_SHAPED.search(m.group(1)) else None
    return comment.strip() if NET_SHAPED.search(comment) else None


def netlogger_table_exists():
    rows = run_sql("show tables like 'netlogger_checkin';")
    return bool(rows)


def identify(path, pad):
    with open(path) as fh:
        side = json.load(fh)

    start = parse_utc(side["started_utc"])
    end = parse_utc(side["ended_utc"])
    lo = (start - timedelta(seconds=pad)).strftime("%Y-%m-%d %H:%M:%S")
    hi = (end + timedelta(seconds=pad)).strftime("%Y-%m-%d %H:%M:%S")

    print(f"\n{side.get('file', os.path.basename(path))}")
    print(f"  {side['started_utc']} → {side['ended_utc']} UTC"
          f"   {side.get('freq_hz_start', 0)/1e6:.4f} MHz {side.get('mode', '?')}"
          f"   ({len(side.get('overs') or [])} overs"
          f"{', not tracked' if side.get('overs') is None else ''})")

    if not side.get("rig_connected"):
        print("  ⚠️ the rig was not connected when this was recorded - no frequency to match on")

    rows = run_sql(
        f"select COL_TIME_ON, COL_CALL, COL_BAND, COL_MODE, COL_FREQ, "
        f"coalesce(COL_COMMENT,'') from {QSO_TABLE} "
        f"where COL_TIME_ON between {sql_str(lo)} and {sql_str(hi)} "
        f"order by COL_TIME_ON;")

    rec_band = band_of(side.get("freq_hz_start") or 0) if side.get("rig_connected") else None

    nets = set()
    if rows:
        print(f"  LOGGED - {len(rows)} QSO(s) in the window (±{pad}s):")
        for t, call, band, mode, freq, comment in rows:
            when = datetime.strptime(t, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
            if when < start:
                off = f"{(start - when).total_seconds():.0f}s before the recording"
            elif when > end:
                off = f"{(when - end).total_seconds():.0f}s after it ended"
            else:
                off = f"{(when - start).total_seconds():.0f}s in"
            net = net_from_comment(comment)
            if net:
                nets.add(net)
            # A different band in the same window is almost certainly a
            # coincidence of time, not this recording. Flagged, never dropped.
            odd = "  ⚠️ DIFFERENT BAND" if rec_band and band and band != rec_band else ""
            print(f"    {call:<10} {band:<5} {mode:<5} {int(freq)/1e6:>9.4f} MHz  "
                  f"logged {off}" + (f"   net: {net}" if net else "") + odd)
    else:
        # ⚠️ Wording matters here. See the module docstring.
        print(f"  LOGGED - nothing found in the window (±{pad}s). That is not evidence "
              f"nobody was worked;\n           an unlogged QSO and a station heard but "
              f"not worked both look like this.")

    if not nets:
        print("  ON THE NET - no net named on any matching QSO, so there is nothing to "
              "look a roster up by.")
        return

    if not netlogger_table_exists():
        print(f"  ON THE NET - net(s) {', '.join(sorted(nets))}, but netlogger_checkin does "
              f"not exist:\n               the poller has never run, so no roster was ever "
              f"captured. NetLogger keeps\n               only 7 days and has no bulk "
              f"history endpoint, so this window cannot be\n               recovered later "
              f"- only nets from the day the poller starts onward.")
        return

    for net in sorted(nets):
        rows = run_sql(
            f"select c.callsign, coalesce(c.first_name,'') from netlogger_checkin c "
            f"join netlogger_net n on n.server=c.server and n.net_id=c.net_id "
            f"where n.net_name={sql_str(net)} and c.callsign<>'' "
            f"and n.started between date_sub({sql_str(lo)}, interval 12 hour) "
            f"and date_add({sql_str(hi)}, interval 12 hour) order by c.callsign;")
        if not rows:
            print(f"  ON THE NET - {net}: no roster stored for this net on this date.")
            continue
        print(f"  ON THE NET - {net}: {len(rows)} checked in. ⚠️ CANDIDATES, not "
              f"identifications -\n               a check-in says present, not that they "
              f"were the voice on the tape.")
        calls = [f"{c}{' (' + n + ')' if n and n != chr(92) + 'N' else ''}" for c, n in rows]
        for i in range(0, len(calls), 4):
            print("               " + "  ".join(f"{c:<18}" for c in calls[i:i + 4]).rstrip())


def main():
    args = [a for a in sys.argv[1:]]
    pad = 120
    if "--pad" in args:
        i = args.index("--pad")
        pad = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    for path in args:
        identify(path, pad)
    print()


if __name__ == "__main__":
    main()
