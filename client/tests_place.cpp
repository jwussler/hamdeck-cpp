// Window placement, against the desktop arrangements a headless box cannot make.
//
// ⚠️ THIS TEST EXISTS BECAUSE 0.1.2 SHIPPED A WINDOW NOBODY COULD MOVE. A fresh
// install placed itself at (0,0); x/y position the client area, so the title bar
// sat above the top of the screen with the close button on it. Every check here
// is a case that would have caught it or one next to it.

#include <QRect>
#include <cstdio>
#include <cstdlib>

#include "src/place.h"

static int failures = 0;
static void Check(const char* what, bool ok, QRect got) {
    std::printf("  %s %-46s -> %d,%d %dx%d\n", ok ? "ok  " : "FAIL", what,
                got.x(), got.y(), got.width(), got.height());
    if (!ok) ++failures;
}

int main() {
    const QRect kNoSaved;

    // A plain 1080p desktop, taskbar at the bottom: work area starts at 0,0.
    {
        const QRect avail(0, 0, 1920, 1040);
        const QRect g = PlaceWindow(kNoSaved, avail, 1.2);
        Check("first run is centred, never in the corner",
              g.x() > 0 && g.y() >= 48 && avail.contains(g), g);
    }

    // ⚠️ The exact bug: an older build saved (0,0).
    {
        const QRect avail(0, 0, 1920, 1040);
        const QRect g = PlaceWindow(QRect(0, 0, 900, 800), avail, 1.0);
        Check("a saved 0,0 is floored below the top", g.y() >= 32, g);
    }

    // ⚠️ Taskbar along the TOP: the work area's origin is not zero.
    {
        const QRect avail(0, 48, 1920, 1032);
        const QRect g = PlaceWindow(kNoSaved, avail, 1.0);
        Check("top taskbar: window sits below the work area top",
              g.y() >= avail.y() + 32 && avail.contains(g), g);
    }

    // Taskbar along the LEFT.
    {
        const QRect avail(72, 0, 1848, 1080);
        const QRect g = PlaceWindow(kNoSaved, avail, 1.0);
        Check("left taskbar: window is right of the work area edge",
              g.x() >= avail.x() && avail.contains(g), g);
    }

    // A second monitor, which lives at a virtual-desktop offset.
    {
        const QRect avail(1920, 0, 2560, 1400);
        const QRect g = PlaceWindow(kNoSaved, avail, 1.6);
        Check("second monitor: placed on THAT screen, not the primary",
              g.x() >= 1920 && avail.contains(g), g);
    }

    // The monitor it was last used on has been unplugged.
    {
        const QRect avail(0, 0, 1920, 1040);
        const QRect g = PlaceWindow(QRect(3200, 400, 900, 800), avail, 1.0);
        Check("a position from a monitor that is gone is re-centred",
              avail.contains(g) && g.x() < 1920, g);
    }

    // Saved bigger than the screen it is restored onto.
    {
        const QRect avail(0, 0, 1024, 600);
        const QRect g = PlaceWindow(QRect(0, 0, 1600, 1200), avail, 0.8);
        Check("oversized saved geometry is clamped, decoration included",
              avail.contains(g) && g.height() <= 600 - 32, g);
    }

    // A tiny screen where the default cannot fit as designed.
    {
        const QRect avail(0, 0, 800, 480);
        const QRect g = PlaceWindow(kNoSaved, avail, 0.8);
        Check("tiny screen still yields a fully on-screen window",
              avail.contains(g), g);
    }

    // ⚠️ The guard against this test quietly passing on nothing: a window that
    // is inside the work area must NOT be moved, or "always on screen" would be
    // satisfied by ignoring the operator's own position every time.
    {
        const QRect avail(0, 0, 1920, 1040);
        const QRect saved(300, 200, 900, 700);
        const QRect g = PlaceWindow(saved, avail, 1.0);
        Check("a good saved position is left alone", g == saved, g);
    }

    std::printf(failures ? "PLACEMENT FAILED\n" : "PLACEMENT PASSED\n");
    return failures ? 1 : 0;
}
