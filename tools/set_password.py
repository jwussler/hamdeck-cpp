#!/usr/bin/env python3
"""Set a HamDeck host user's password, or add a user, without the admin API.

The admin routes are the normal way to do this - but they need an admin session,
and the one time you most need to change a password is the time you cannot log
in to ask for one. This edits the config directly and is deliberately careful:

  * It PRESERVES KEYS IT DOES NOT KNOW. A writer that serialises its own idea of
    the file silently deletes every setting a newer build added and every note
    the operator left. The host's own config writer has this rule; so does this.
  * It writes via a TEMP FILE AND A RENAME, so an interrupted write cannot leave
    a half-written config that then refuses to parse on the next start - and a
    host that will not start is worse than one with a forgotten password.
  * It takes the password from a PROMPT, never an argument: a password on a
    command line is in the shell history and in `ps` for anyone on the box.
  * It backs the file up first.

The hash parameters MUST match src/auth.cpp - PBKDF2-HMAC-SHA256, 350000
iterations, 16-byte salt, 32-byte hash, stored as pbkdf2:<salt hex>:<hash hex>.
If you change them there, change them here, or every password stops verifying.

⚠️ The host reads users AT STARTUP, so a change here needs a restart:
      sudo systemctl restart hamdeck-cpp
   That drops CAT for a couple of seconds and ends live sessions. The host
   unkeys on shutdown and confirms it by reading TX; back, so it is safe to do
   while the radio is attached - but do not do it mid-transmission.

Usage:
    tools/set_password.py joe                  # change a password
    tools/set_password.py newop --add          # add a user (no transmit)
    tools/set_password.py newop --add --transmit --admin
    HAMDECK_CONFIG=/path/to/config.json tools/set_password.py joe
"""

import argparse
import getpass
import hashlib
import json
import os
import shutil
import sys
import time

ITERATIONS = 350_000          # must match kIterations in src/auth.cpp
SALT_BYTES = 16               # kSaltBytes
HASH_BYTES = 32               # kHashBytes
DEFAULT_CONFIG = "/etc/hamdeck-cpp/config.json"


def hash_password(password: str) -> str:
    salt = os.urandom(SALT_BYTES)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode(), salt,
                                 ITERATIONS, HASH_BYTES)
    return f"pbkdf2:{salt.hex()}:{digest.hex()}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("username")
    ap.add_argument("--add", action="store_true", help="create the user if absent")
    ap.add_argument("--admin", action="store_true", help="grant admin (with --add)")
    ap.add_argument("--transmit", action="store_true", help="grant transmit (with --add)")
    ap.add_argument("--config", default=os.environ.get("HAMDECK_CONFIG", DEFAULT_CONFIG))
    args = ap.parse_args()

    try:
        with open(args.config) as f:
            cfg = json.load(f)
    except FileNotFoundError:
        print(f"no config at {args.config} - set HAMDECK_CONFIG or pass --config",
              file=sys.stderr)
        return 1
    except json.JSONDecodeError as e:
        # Refuse rather than "fix" it. A config we cannot parse is one we cannot
        # safely rewrite, and guessing would lose settings.
        print(f"{args.config} is not valid JSON ({e}) - refusing to touch it",
              file=sys.stderr)
        return 1

    users = cfg.setdefault("web_users", [])
    target = next((u for u in users if u.get("username") == args.username), None)
    if target is None and not args.add:
        names = ", ".join(u.get("username", "?") for u in users) or "(none)"
        print(f"no user '{args.username}'. Users: {names}. Use --add to create one.",
              file=sys.stderr)
        return 1

    pw = getpass.getpass(f"New password for {args.username}: ")
    if len(pw) < 8:
        print("refusing a password under 8 characters", file=sys.stderr)
        return 1
    if pw != getpass.getpass("Again: "):
        print("they do not match", file=sys.stderr)
        return 1

    known = set(cfg.keys())
    if target is None:
        users.append({"username": args.username, "password_hash": hash_password(pw),
                      "is_admin": args.admin, "can_transmit": args.transmit})
        what = f"added {args.username} (admin={args.admin}, transmit={args.transmit})"
    else:
        target["password_hash"] = hash_password(pw)
        if args.admin:
            target["is_admin"] = True
        if args.transmit:
            target["can_transmit"] = True
        what = (f"password changed for {args.username} "
                f"(admin={target.get('is_admin')}, transmit={target.get('can_transmit')})")

    # Nothing above may drop a key. If it did, stop before writing.
    if set(cfg.keys()) != known:
        print("internal error: config keys changed - refusing to write", file=sys.stderr)
        return 1

    backup = f"{args.config}.bak-{time.strftime('%m%d-%H%M%S')}"
    shutil.copy2(args.config, backup)

    tmp = f"{args.config}.tmp"
    with open(tmp, "w") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, args.config)

    print(f"{what}\nbacked up to {backup}")
    print("⚠️  users are read at startup - run: sudo systemctl restart hamdeck-cpp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
