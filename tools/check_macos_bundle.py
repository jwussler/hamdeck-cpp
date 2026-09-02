#!/usr/bin/env python3
"""Is the built .app actually a named, iconned, mic-capable Mac application?

⚠️ WRITTEN BECAUSE 0.1.29 SHIPPED SIGNED, NOTARISED AND WRONG. The DMG installed
and the app ran, and macOS called it "hamdeck-qml" and drew it with the blank
generic-document icon, because CMake names a bundle after the target and its
stock Info.plist has no icon key. Nothing failed. There was no default that could
have been right, and no check that looked.

It also covers the one that had not bitten yet: an app with the microphone
ENTITLEMENT but no NSMicrophoneUsageDescription is SIGKILLed by macOS the moment
it opens the mic. That is the first PTT of the day, on the operator's machine,
with nothing in the log - so it is checked here, on every push, on Linux too.

Everything is read with plistlib and struct rather than plutil/iconutil, so this
same gate runs on the Linux CI leg and on a workstation with no Xcode.

Usage: check_macos_bundle.py <path-to-.app> [expected name]
"""
import os, plistlib, struct, sys

# The OSTypes brand/build.sh packs, and the pixel size each one must contain.
ICNS_TYPES = {b'icp4': 16, b'ic11': 32, b'icp5': 32, b'ic12': 64, b'ic07': 128,
              b'ic13': 256, b'ic08': 256, b'ic14': 512, b'ic09': 512, b'ic10': 1024}


def check_icns(path, fails):
    d = open(path, 'rb').read()
    if d[:4] != b'icns':
        fails.append(f"{path} is not an icns (magic {d[:4]!r})")
        return
    declared = struct.unpack('>I', d[4:8])[0]
    if declared != len(d):
        fails.append(f"icns declares {declared} bytes but the file is {len(d)}")
    off, seen = 8, {}
    while off + 8 <= len(d):
        t = d[off:off + 4]
        n = struct.unpack('>I', d[off + 4:off + 8])[0]
        if n < 8 or off + n > len(d):
            fails.append(f"icns entry {t!r} has a bad length {n}")
            return
        blob = d[off + 8:off + n]
        off += n
        # ⚠️ Trust the PNG's own header, not the slot it was filed under. An icns
        # holding the 512px art under ic10 is structurally perfect and looks soft
        # on exactly the Retina display the 1024 entry exists for.
        if blob[:8] == b'\x89PNG\r\n\x1a\n':
            w, h = struct.unpack('>II', blob[16:24])
            want = ICNS_TYPES.get(t)
            if want and (w, h) != (want, want):
                fails.append(f"icns {t.decode()}: artwork is {w}x{h}, the slot needs {want}x{want}")
            seen[t] = (w, h)
        else:
            fails.append(f"icns {t.decode()} is not PNG data")
    missing = sorted(t.decode() for t in ICNS_TYPES if t not in seen)
    if missing:
        fails.append("icns is missing types: " + " ".join(missing) +
                     " (macOS then scales a smaller entry up, on Retina, forever)")
    return len(seen)


def main():
    if not 2 <= len(sys.argv) <= 3:
        print("usage: check_macos_bundle.py <path-to-.app> [expected name]")
        return 2
    app = sys.argv[1].rstrip('/')
    want_name = sys.argv[2] if len(sys.argv) == 3 else "HamDeck Remote"
    fails = []

    if not os.path.isdir(app):
        print(f"FAIL: no bundle at {app}")
        return 1
    base = os.path.basename(app)
    if base != f"{want_name}.app":
        fails.append(f"the bundle is called {base}, not {want_name}.app "
                     f"- OUTPUT_NAME did not apply, and Finder shows this name")

    plist_path = os.path.join(app, "Contents", "Info.plist")
    if not os.path.isfile(plist_path):
        print(f"FAIL: no Info.plist in {app}")
        return 1
    with open(plist_path, 'rb') as fh:
        info = plistlib.load(fh)

    for key in ("CFBundleName", "CFBundleDisplayName"):
        got = info.get(key)
        if got != want_name:
            fails.append(f"{key} is {got!r}, not {want_name!r} "
                         f"(this is the name in the menu bar and under the Dock icon)")
    if not info.get("CFBundleIdentifier"):
        fails.append("CFBundleIdentifier is empty")
    if not info.get("CFBundleShortVersionString"):
        fails.append("CFBundleShortVersionString is empty - the Finder Get Info version")
    if info.get("NSHighResolutionCapable") is not True:
        fails.append("NSHighResolutionCapable is not true - the app renders at 1x and is scaled")

    mic = info.get("NSMicrophoneUsageDescription")
    if not mic:
        fails.append("NSMicrophoneUsageDescription is MISSING - macOS SIGKILLs the app "
                     "on the first PTT, with no prompt and no log line")

    exe = info.get("CFBundleExecutable")
    exe_path = os.path.join(app, "Contents", "MacOS", exe or "")
    if not exe or not os.path.isfile(exe_path):
        fails.append(f"CFBundleExecutable is {exe!r}, and Contents/MacOS/{exe} is not a file")

    entries = None
    icon = info.get("CFBundleIconFile")
    if not icon:
        fails.append("CFBundleIconFile is MISSING - this is the blank generic icon, exactly "
                     "what 0.1.29 shipped")
    else:
        if not icon.endswith(".icns"):
            icon += ".icns"
        icon_path = os.path.join(app, "Contents", "Resources", icon)
        if not os.path.isfile(icon_path):
            fails.append(f"CFBundleIconFile names {icon}, which is not in Contents/Resources "
                         f"- the icon was added at INSTALL time, not build time")
        else:
            entries = check_icns(icon_path, fails)

    if fails:
        print(f"FAIL: {os.path.basename(app)} is not a properly formed Mac application")
        for f in fails:
            print(f"  - {f}")
        return 1
    print(f'ok: "{base}" - CFBundleName/DisplayName {want_name!r}, id {info["CFBundleIdentifier"]}, '
          f'v{info["CFBundleShortVersionString"]}, {entries} icns entries, mic string present')
    return 0


if __name__ == "__main__":
    sys.exit(main())
