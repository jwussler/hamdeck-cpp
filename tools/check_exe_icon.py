#!/usr/bin/env python3
"""Is the application icon actually INSIDE the built .exe?

⚠️ WRITTEN BECAUSE THE ANSWER WAS NO WHILE EVERYTHING WAS GREEN. The .rc was
committed and referenced from CMakeLists, but project() declared LANGUAGES CXX,
so CMake silently ignored the .rc and the Windows build succeeded with no icon in
the binary. Nothing looked, so nothing failed.

The .ico's 256x256 entry is PNG data, and the resource compiler copies it into
RT_ICON verbatim - so a distinctive slice of that PNG appears byte-for-byte in
the exe when, and only when, the resource was really compiled in.

Usage: check_exe_icon.py <exe> <png-that-is-the-256px-icon>
"""
import sys

def main():
    if len(sys.argv) != 3:
        print("usage: check_exe_icon.py <exe> <256px png>")
        return 2
    exe_path, png_path = sys.argv[1], sys.argv[2]
    png = open(png_path, "rb").read()
    exe = open(exe_path, "rb").read()

    # Skip the PNG header - it is not distinctive. Take a slice out of the middle
    # of the compressed image data, which appears in no other file.
    if len(png) < 300:
        print(f"FAIL: {png_path} is only {len(png)} bytes; too small to fingerprint")
        return 1
    needle = png[100:160]

    if needle in exe:
        print(f"ok: the icon artwork is embedded in {exe_path} "
              f"({len(exe)} bytes, matched a 60-byte slice of {png_path})")
        return 0
    print(f"FAIL: {exe_path} does NOT contain the icon artwork.")
    print("      The .rc was not compiled in - check enable_language(RC) and that")
    print("      the .rc is in target_sources. A successful build does not mean")
    print("      the resource was built; CMake ignores a .rc silently without RC.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
