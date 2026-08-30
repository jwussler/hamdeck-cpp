# Packaging, shared by the host and the client.
#
# ⚠️ Qt is LGPLv3, so it is a DEPENDENCY, not a bundled copy. A .deb that
# declares the Qt runtime keeps the dynamic-linking obligation satisfied by
# construction, and keeps the package small. Bundling Qt would need either a
# commercial licence or shipping relinkable objects.
set(CPACK_PACKAGE_VENDOR "HamDeck")
set(CPACK_PACKAGE_CONTACT "HamDeck <noreply@example.invalid>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jwussler/hamdeck-cpp")
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

# ⚠️ PRE-RELEASE, AND THE PACKAGE SAYS SO. BRAND.md's voice section: name what
# does not work; that earns more trust than claiming readiness. One rig has been
# tested, the installers are unsigned, and the description says both.
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Remote HF radio control - pre-release, tested on one rig")
