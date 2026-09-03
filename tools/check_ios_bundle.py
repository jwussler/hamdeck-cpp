#!/usr/bin/env python3
"""Does the iOS bundle declare everything Apple's delivery scan will look for?

⚠️ WRITTEN BECAUSE 0.1.0 WAS REJECTED AFTER A PERFECT BUILD. It compiled, linked,
signed, exported and uploaded; the rejection arrived by email minutes later:

    ITMS-90683: Missing purpose string in Info.plist - ... the "hamdeck-qml.app"
    bundle should contain a NSCameraUsageDescription key ...

Nothing in this repo looked. The app has no camera and never opens one, but Qt
for iOS is STATIC: QDarwinMediaPlugin, the AVFoundation backend that QAudioSource
and QAudioSink need, is linked whole and brings AVCaptureDevice with it. Apple
scans the LINKED BINARY, not the code paths, so the string is required anyway.

So this reads the same two things Apple reads and refuses when they disagree:

  1. the Info.plist keys the app cannot work (or ship) without, and
  2. the protected-API classes actually referenced by the built executable,
     each of which demands its own purpose string.

Both modes are pure Python - plistlib plus a byte scan - so the template check
runs on the Linux CI leg and on a workstation with no Xcode, and the bundle check
runs on the macOS/iOS job right after the build.

Usage:
  check_ios_bundle.py --template client/packaging/ios/Info.plist.in
  check_ios_bundle.py <path-to-.app>
"""
import os, plistlib, re, sys

# ── The keys that must be there, and what breaks when they are not ───────────
# ⚠️ EVERY ENTRY HERE COST SOMETHING. None of them fail at build time.
REQUIRED = {
    "CFBundleIdentifier":        "the profile, the signature and App Store Connect all key off it",
    "CFBundleExecutable":        "a bundle whose executable name is wrong installs and never launches",
    "CFBundleName":              "the name under the home-screen icon",
    "CFBundleShortVersionString": "the version App Store Connect files the build under",
    "CFBundleVersion":           "the BUILD number; Apple refuses a second upload that reuses one",
    "CFBundleIconName":          "altool 90713 - names the asset catalog's icon SET, not a file",
    "MinimumOSVersion":          "iOS refuses to install a bundle that does not say what it needs",
    "LSRequiresIPhoneOS":        "without it the bundle is not treated as an iOS app",
    "ITSAppUsesNonExemptEncryption": "without it the build lands and silently waits for a human",
    "UILaunchScreen":            "no launch screen letterboxes the app into a phantom smaller screen",
    "UIBackgroundModes":         "without 'audio' iOS suspends the app when the screen locks",
}

# ── Protected APIs, and the purpose string each one obliges ──────────────────
# ⚠️ THE KEY IS THE OBJC CLASS AS IT APPEARS IN THE BINARY. Apple's scan is a
# symbol scan; a class that is merely LINKED counts, which is the whole reason
# 0.1.0 was rejected. Referenced-but-unused is not a defence, and there is no way
# to argue with an automated delivery check.
API_KEYS = {
    "AVCaptureDevice":        "NSCameraUsageDescription",
    "AVCaptureSession":       "NSCameraUsageDescription",
    "AVCaptureVideoDataOutput": "NSCameraUsageDescription",
    "AVAudioSession":         "NSMicrophoneUsageDescription",
    "AVAudioRecorder":        "NSMicrophoneUsageDescription",
    "CLLocationManager":      "NSLocationWhenInUseUsageDescription",
    "CBCentralManager":       "NSBluetoothAlwaysUsageDescription",
    "CBPeripheralManager":    "NSBluetoothAlwaysUsageDescription",
    "PHPhotoLibrary":         "NSPhotoLibraryUsageDescription",
    "UIImagePickerController": "NSPhotoLibraryUsageDescription",
    "CNContactStore":         "NSContactsUsageDescription",
    "EKEventStore":           "NSCalendarsUsageDescription",
    "CMMotionManager":        "NSMotionUsageDescription",
    "SFSpeechRecognizer":     "NSSpeechRecognitionUsageDescription",
    "LAContext":              "NSFaceIDUsageDescription",
}

# What this app knows it links and has answered for. Anything ELSE the scan finds
# is a new obligation that nobody has thought about yet, so it fails loudly
# rather than being waved through by a key that happens to exist.
EXPECTED_APIS = {"AVCaptureDevice", "AVCaptureSession", "AVCaptureVideoDataOutput",
                 "AVAudioSession", "AVAudioRecorder"}


def load_template(path, fails):
    """Parse the CMake template. ${...} placeholders are just strings to plistlib."""
    try:
        with open(path, 'rb') as fh:
            return plistlib.load(fh)
    except Exception as e:
        fails.append(f"{path} is not a parseable plist: {e}")
        return None


def required_keys():
    """The keys any build of THIS app must carry.

    ⚠️ THE PURPOSE STRINGS ARE DERIVED, NOT LISTED. Every class in EXPECTED_APIS
    is something this app is known to link, and each one obliges its string
    whether or not the app ever calls it - that is exactly what ITMS-90683 said
    about the camera. Deriving them means adding a linked framework to
    EXPECTED_APIS also demands its string, instead of the two lists drifting
    apart until Apple points out the gap.
    """
    keys = dict(REQUIRED)
    for cls in sorted(EXPECTED_APIS):
        keys.setdefault(API_KEYS[cls],
                        f"the binary links {cls}; Apple rejects the upload without it "
                        f"(ITMS-90683), used or not")
    # ATS is opened for local networking, and iOS 14+ prompts for it.
    keys.setdefault("NSLocalNetworkUsageDescription",
                    "the app finds the station host on the LAN; without it the prompt has no text")
    return keys


def check_keys(info, fails, template):
    for key, why in required_keys().items():
        if key not in info:
            fails.append(f"{key} is MISSING - {why}")
            continue
        val = info[key]
        if isinstance(val, str) and not val.strip():
            fails.append(f"{key} is present but EMPTY - {why}")
        # ⚠️ A ${...} placeholder is CORRECT in the template and a BUG in a built
        # bundle: it means CMake never substituted, and the app ships with a
        # literal ${MACOSX_BUNDLE_GUI_IDENTIFIER} as its bundle id.
        if not template and isinstance(val, str) and re.fullmatch(r"\$\{[^}]*\}", val.strip()):
            fails.append(f"{key} is still the unsubstituted placeholder {val!r}")

    modes = info.get("UIBackgroundModes") or []
    if "audio" not in modes:
        fails.append("UIBackgroundModes does not contain 'audio' - RX dies when the screen locks")

    # ⚠️ A purpose string that exists but says nothing is rejected by App Review
    # as surely as a missing one, and it is the operator who reads it.
    for key, val in info.items():
        if key.endswith("UsageDescription") and len(str(val).strip()) < 10:
            fails.append(f"{key} is too short to be a purpose string: {val!r}")


def scan_binary(app, fails):
    """Which protected-API classes does the built executable actually reference?"""
    plist_path = os.path.join(app, "Info.plist")
    info = None
    if os.path.isfile(plist_path):
        with open(plist_path, 'rb') as fh:
            info = plistlib.load(fh)
    if info is None:
        fails.append(f"no Info.plist at the top of {app} (an iOS bundle is FLAT)")
        return None, set()

    exe = info.get("CFBundleExecutable")
    exe_path = os.path.join(app, exe or "")
    if not exe or not os.path.isfile(exe_path):
        fails.append(f"CFBundleExecutable is {exe!r} and {exe_path} is not a file")
        return info, set()

    blob = open(exe_path, 'rb').read()
    found = {cls for cls in API_KEYS if cls.encode() in blob}
    for cls in sorted(found):
        key = API_KEYS[cls]
        if not str(info.get(key, "")).strip():
            fails.append(f"the binary references {cls} and {key} is MISSING - "
                         f"this is ITMS-90683, and it is found at UPLOAD, not at build")
    unexpected = found - EXPECTED_APIS
    if unexpected:
        fails.append("the binary now references protected APIs nobody has answered for: "
                     + ", ".join(sorted(unexpected))
                     + " - decide whether the app should link them at all before adding a string")
    return info, found


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    fails = []
    if args[0] == "--template":
        if len(args) != 2:
            print("usage: check_ios_bundle.py --template <Info.plist.in>")
            return 2
        info = load_template(args[1], fails)
        if info is not None:
            check_keys(info, fails, template=True)
        what, found = f"template {args[1]}", None
    else:
        app = args[0].rstrip('/')
        if not os.path.isdir(app):
            print(f"FAIL: no bundle at {app}")
            return 1
        info, found = scan_binary(app, fails)
        if info is not None:
            check_keys(info, fails, template=False)
        what = f"bundle {os.path.basename(app)}"

    if fails:
        print(f"FAIL: {what} would not survive an App Store delivery")
        for f in fails:
            print(f"  - {f}")
        return 1
    strings = sorted(k for k in (info or {}) if k.endswith("UsageDescription"))
    extra = f", binary references {len(found)} protected-API classes" if found is not None else ""
    print(f"ok: {what} - {len(required_keys())} required keys present, "
          f"purpose strings: {' '.join(strings) or '(none)'}{extra}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
