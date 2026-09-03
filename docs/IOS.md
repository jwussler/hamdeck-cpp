# HamDeck on iOS

The Qt Quick client, on a phone. This is the state of it, the one decision that
is not mine to make, and the exact steps that need an Apple account.

---

## Where it stands

| | |
|---|---|
| C++ / QML source | **No port needed.** Qt Quick, Qt Multimedia and Qt WebSockets are all supported on iOS, and the only non-portable code (`src/global_hotkey.cpp`, three `#ifdef _WIN32` blocks in `main_qml.cpp`) is already guarded and meaningless on a handset. |
| CMake | **Done.** `client/CMakeLists.txt` has an `if(IOS)` bundle block; `-DHAMDECK_IOS_BUNDLE_ID=` sets the identifier. |
| `Info.plist` | **Done.** `client/packaging/ios/Info.plist.in`, gated by `tools/check_ios_bundle.py` — see the rejections section below. |
| Device build in CI | ✅ **Green.** `.github/workflows/ios.yml`, unsigned, so it needs no App ID or profile — it proves the client compiles and links for a phone and uploads the `.app` (~12.8 MB). ⚠️ **It does not run the app** — see below. |
| Device build | Needs the portal work below. |
| TestFlight | Needs the portal work **and** the licence decision below. |

---

## ⚠️ The decision that is not mine: Qt for iOS is static-only

There is no shared-library Qt on iOS. Apple does not permit third-party dynamic
libraries in App Store binaries, so Qt ships iOS as static libraries only.

`client/CMakeLists.txt` has carried this warning since long before any of this:

> Qt is LGPLv3 here. That is fine for this repo, but it constrains DISTRIBUTION:
> link Qt dynamically (the default below) and ship the licence texts. Statically
> linking Qt without a commercial licence is the mistake to avoid, and it is one
> you only find out about at release time.

iOS is the one platform where that is unavoidable. It is **not a wall** — LGPLv3
allows static linking if recipients can relink the application against a modified
Qt, which in practice means publishing the object files or a linkable archive
alongside the app. People do ship LGPL Qt apps on the App Store this way. But it
is an obligation, it applies the moment the app is **distributed**, and TestFlight
is distribution.

**None of this touches development.** A simulator build, a build on your own
device, and everything the CI job does are not distribution. So the work below is
worth doing regardless; the decision only has to be made before the first
TestFlight upload.

The alternatives, briefly and without a recommendation, because this is a
business call: publish the relinkable objects, or buy a Qt commercial licence.

---

## What only you can do (Apple portal)

Everything here needs a signed-in Apple Developer account. Team **V829EBE8HH**.

1. **Register an App ID.** Identifiers → App IDs → new, explicit, bundle ID
   `com.wa0o.hamdeck`. It needs no special capabilities — background audio is a
   `UIBackgroundModes` key in the plist, not an entitlement.
2. **Create an App Store distribution provisioning profile** against that App ID
   and the iOS distribution certificate cut on 09/01/2026. Download the
   `.mobileprovision`.
3. **Create the App Store Connect record** — a new app, same bundle ID, with a
   name and a primary language. **TestFlight cannot accept a build for an app
   record that does not exist**, and the upload failure does not say so clearly.
4. Add the profile and the distribution `.p12` to the repo's GitHub secrets, the
   way `APPLE_CERT_P12` already is for the Mac job.

⚠️ **Internal TestFlight testing needs no App Review.** External testers do.
For a first build on your own phone, internal is the whole story.

---

## What is already on the box

`/home/ubuntu/secure/apple/` — **not in git, and keep it that way.**

- `ios/ios_dist.cer`, `ios_dist.key`, `ios_dist.csr` — the iOS distribution
  identity, generated 09/01/2026.
- `AuthKey_8WPAG839Q7.p8` — App Store Connect API key (Admin). This is what
  uploads a build; Apple lets a `.p8` be downloaded exactly once.
- `devid.p12` / `devid.cer` / `devid.key` — Developer **ID** (Mac, outside the
  store). Not usable for iOS.

The App Store Connect issuer ID is in the gitignored `SITE.md`.

⚠️ Developer Program membership expires **02/01/2027**. Certificates die with the
membership, not on their own five-year clock.

---

## Running it

**In CI** — push to a branch named `ios*` or open a pull request touching
`client/`, and `.github/workflows/ios.yml` builds an unsigned device bundle and
asserts on its `Info.plist`.

### Two link traps, both found the hard way

Everything compiled on the first attempt. Both failures were at **link**, which
is why each one reads as a code problem and neither is.

1. **The FFmpeg media backend.** Qt's iOS package ships
   `libQt6FFmpegMediaPluginImpl.a` but not the FFmpeg libraries it calls, so a
   static link ends in a wall of undefined `_sws_*` and `_av_*` symbols naming
   video code this app never touches. A static build auto-imports every backend.
   Apple platforms have a native one — `plugins/multimedia/` in the iOS install
   contains exactly `libdarwinmediaplugin.a` and nothing else — and it is the
   correct backend on a phone regardless. `client/CMakeLists.txt` excludes the
   FFmpeg plugin and pins the Darwin one.

2. **The permission plugins are device-only**, which is what blocks the
   simulator. Below.

### ⚠️ Nothing has run this app yet, and that is a real gap

CI proves it **compiles and links**. It does not prove it **starts**. A static Qt
build missing its QML plugin imports links perfectly and dies at launch with
"module QtQuick is not installed", and this repo has already shipped one client
that could not launch while every check was green. Treat "the iOS job is green"
as meaning exactly what it says and no more.

The simulator build is what would have closed that gap — it needs no signing, so
CI could boot a simulator and launch the app. It does not link:

```
ld: building for 'iOS-simulator', but linking in object file
    (.../ios/plugins/permissions/objects-Release/
     QDarwinMicrophonePermissionPlugin_init/QDarwinMicrophonePermissionPlugin_init.cpp.o)
    built for 'iOS'
```

Qt's iOS package ships the permission plugins' `_init` objects as **device slices
only**. `qt_import_plugins(hamdeck-qml EXCLUDE_BY_TYPE permissions)` does not
remove them — they arrive through QtMultimedia's own link interface. The
`HAMDECK_IOS_SIMULATOR` CMake option exists and does that exclusion, for whenever
this is solved. Things worth trying, none of them tried yet: the official Qt
online installer instead of `aqt` (its iOS package may carry both slices), a Qt
version other than 6.9.2, or building QtMultimedia without the permissions
plugin.

**Until then the first proof that this app runs comes from your own phone**, via
Xcode on a Mac. That is not a bad first step anyway — it is the same step that
gets you a device build, and it needs the App ID below.

**Locally, if you ever have a Mac:**

```
cmake -S client -B client/build-ios -G Xcode \
  -DCMAKE_TOOLCHAIN_FILE=<ios-qt>/lib/cmake/Qt6/qt.toolchain.cmake \
  -DQT_HOST_PATH=<host-qt> \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build client/build-ios --config Release
```

---

## ⚠️ Delivery rejections — what Apple checks after a perfect build

Everything from `cmake` to `altool` can pass and the app still be refused, by
email, minutes later. Three have happened; all three were the **bundle**, none
were the code, and none of them failed anything local at the time.

| | |
|---|---|
| **90713 / 90022 / 90023** | No `CFBundleIconName`, no 120x120, no 152x152. Fixed by the compiled asset catalog — a loose PNG never enters the bundle. |
| **ITMS-90683** | `NSCameraUsageDescription` missing. 09/03/2026, version 0.1.0. |

### ITMS-90683, and why an app with no camera needs a camera string

Qt for iOS is **static**. `QDarwinMediaPlugin` — the AVFoundation backend that
`QAudioSource` and `QAudioSink` run on, and the only one on a phone that can
reach the microphone at all — is linked whole, and it carries `AVCaptureDevice`
with it. **Apple scans the linked binary, not the code paths.** A class that is
merely present obliges its purpose string, whether or not anything ever calls it,
and there is no arguing with an automated delivery check.

So the string is in `client/packaging/ios/Info.plist.in` and it says plainly that
the camera is not used and why the key is there anyway. Dropping the plugin is
not the alternative: it would take the microphone with it.

**The gate is `tools/check_ios_bundle.py`**, which reads the same two things
Apple reads — the plist keys, and the protected-API classes actually present in
the built executable — and refuses when they disagree. It runs three times:

- on the **Linux** job, against the template, on every push (`--template`);
- on the **device** job, against the built `.app`;
- on the **signed** job, against the bundle **inside the exported `.ipa`** — the
  exact bytes Apple receives.

It also fails when the binary starts referencing a protected API nobody has
answered for yet, which is the version of this that has not bitten: the next
Qt module that quietly links `CLLocationManager` would otherwise be found by
Apple, not here.

### ⚠️ A rejected upload still burns its build number

`CFBundleShortVersionString` is the version a user sees; **`CFBundleVersion` is
the build number, and it may never repeat** — including after a rejection, where
App Store Connect shows nothing to explain what the collision is with. The two
are separate inputs now: `-DHAMDECK_VERSION=` and `-DHAMDECK_IOS_BUILD=`, wired
to the `version` and `build` inputs of the `ios` workflow. **Raise `build` on
every dispatch.** 0.1.0 build `0.1.0` is spent; the next upload is build `2`.

---

## Expect the UI to be wrong at first, and that it is not a bug

The QML panel was drawn for a desktop window. On a handset it will be small,
its touch targets will be sized for a mouse, and it knows nothing about the
notch. `Theme.u()` / `Theme.cols()` (CLAUDE.md) are the mechanisms that already
exist for this; safe-area insets are the one genuinely new piece.

Get it launching first. Judge the layout from a screenshot on a real screen size
rather than from reading the QML — that is what `--check-resolutions` is for, and
adding phone sizes to its list is the cheap way in.
