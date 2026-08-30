#pragma once

// Where the window goes. Pure arithmetic, no Qt window, no screen lookup - so
// it can be tested against the cases a desktop produces but a headless box
// cannot: a taskbar along the top, a monitor that has been unplugged, a saved
// size larger than the screen it is being restored onto.
//
// ⚠️ THE WORK AREA HAS AN ORIGIN, NOT JUST A SIZE. availableGeometry() on a
// desktop with the taskbar on the left or the top starts at x>0 or y>0, and on
// a second monitor it starts wherever that monitor sits in the virtual desktop.
// Placing inside 0..width is right only on the one arrangement we happen to
// have, and wrong under a top taskbar - which is exactly the class of mistake
// that put 0.1.2's window under the title bar.

#include <QRect>

// `saved` may be invalid (a first run) or nonsense (a monitor that no longer
// exists). `avail` is the work area, origin included. `scale` is the UI scale,
// which decides both the default size and how much room a decoration needs.
QRect PlaceWindow(const QRect& saved, const QRect& avail, qreal scale);
