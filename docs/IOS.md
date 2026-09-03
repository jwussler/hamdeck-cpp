# HamDeck on iOS

The Qt Quick client, on a phone. This is the state of it, the one decision that
is not mine to make, and the exact steps that need an Apple account.

---

## Where it stands

| | |
|---|---|
| C++ / QML source | **No port needed.** Qt Quick, Qt Multimedia and Qt WebSockets are all supported on iOS, and the only non-portable code (`src/global_hotkey.cpp`, three `#ifdef _WIN32` blocks in `main_qml.cpp`) is already guarded and meaningless on a handset. |
| CMake | **Done.** `client/CMakeLists.txt` has an `if(IOS)` bundle block; `-DHAMDECK_IOS_BUNDLE_ID=` sets the identifier. |
| `Info.plist` | **Done.** `client/packaging/ios/Info.plist.in`. |
| Simulator build + launch in CI | **`.github/workflows/ios.yml`.** No signing, no App ID, no profile — it can run today. |
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
`client/`, and `.github/workflows/ios.yml` builds for the simulator, boots one,
installs the app, launches it with `--selftest`, and uploads a screenshot.

⚠️ **The launch step is the point, not the build.** A static Qt build that is
missing its QML plugin imports links perfectly and then dies at startup with
"module QtQuick is not installed". Only running it tells the two apart, and this
repo has already shipped one client that could not launch while every check was
green.

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

## Expect the UI to be wrong at first, and that it is not a bug

The QML panel was drawn for a desktop window. On a handset it will be small,
its touch targets will be sized for a mouse, and it knows nothing about the
notch. `Theme.u()` / `Theme.cols()` (CLAUDE.md) are the mechanisms that already
exist for this; safe-area insets are the one genuinely new piece.

Get it launching first. Judge the layout from a screenshot on a real screen size
rather than from reading the QML — that is what `--check-resolutions` is for, and
adding phone sizes to its list is the cheap way in.
