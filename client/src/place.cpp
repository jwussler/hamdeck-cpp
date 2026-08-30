#include "place.h"

#include <QtGlobal>

QRect PlaceWindow(const QRect& saved, const QRect& avail, qreal scale) {
  if (avail.width() <= 0 || avail.height() <= 0) return saved;   // nothing to place against

  // ⚠️ x/y position the CLIENT AREA, so the decoration hangs ABOVE y. Leave it
  // room or the title bar is off the screen and the window cannot be dragged or
  // closed - which is what 0.1.2 shipped. Scaled, because a title bar on a
  // HiDPI desktop is taller in device-independent pixels; and deliberately
  // generous, because being wrong high costs a small gap and being wrong low
  // costs an unusable window.
  // ⚠️ A DECORATION HAS FOUR SIDES. Reserving only the top was the second
  // version of this bug: measured under openbox, a window whose client area
  // starts at x=0 has its left border at x=-1, and on Windows the invisible
  // resize border is around 8 px - so the edge you grab to resize is off the
  // screen even though the title bar is visible.
  //
  // Scaled, and deliberately generous: being wrong high costs a small gap,
  // being wrong low costs a piece of window the operator cannot reach.
  const int frame  = qMax(32, qRound(40 * scale));   // title bar, above y
  const int border = qMax(8, qRound(8 * scale));     // left, right and bottom

  const bool fresh = !saved.isValid() || saved.width() < 320 || saved.height() < 240;

  // The default size scales with the panel inside it. A fixed 880x760 is most
  // of a laptop and a postage stamp on a 4K monitor.
  int w = fresh ? qRound(880 * scale) : saved.width();
  int h = fresh ? qRound(760 * scale) : saved.height();
  w = qBound(320, w, qMax(320, avail.width() - border * 2));
  h = qBound(240, h, qMax(240, avail.height() - frame - border));

  auto centred = [&](int extent, int avail_extent, int origin) {
    return origin + (avail_extent - extent) / 2;
  };

  int x = fresh ? centred(w, avail.width(), avail.x()) : saved.x();
  int y = fresh ? centred(h, avail.height(), avail.y()) : saved.y();

  // Off the work area entirely: an unplugged monitor, or a position saved from
  // a larger desktop. Re-centre rather than clamp - a window shoved back from
  // 3000 px to the right edge is a window nobody put there.
  if (x < avail.x() || y < avail.y() ||
      x + w > avail.right() + 1 || y + h > avail.bottom() + 1) {
    x = centred(w, avail.width(), avail.x());
    y = centred(h, avail.height(), avail.y());
  }

  // The floors, applied last so nothing above can undo them. All four are
  // relative to the WORK AREA, not the screen: with a taskbar along the top,
  // avail.y() is already below it and the decoration needs room below that.
  y = qMax(y, avail.y() + frame);
  x = qMax(x, avail.x() + border);
  if (y + h + border > avail.bottom() + 1)
    y = qMax(avail.y() + frame, avail.bottom() + 1 - border - h);
  if (x + w + border > avail.right() + 1)
    x = qMax(avail.x() + border, avail.right() + 1 - border - w);

  return QRect(x, y, w, h);
}
