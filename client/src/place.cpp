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
  const int frame = qMax(32, qRound(40 * scale));

  const bool fresh = !saved.isValid() || saved.width() < 320 || saved.height() < 240;

  // The default size scales with the panel inside it. A fixed 880x760 is most
  // of a laptop and a postage stamp on a 4K monitor.
  int w = fresh ? qRound(880 * scale) : saved.width();
  int h = fresh ? qRound(760 * scale) : saved.height();
  w = qBound(320, w, avail.width());
  h = qBound(240, h, qMax(240, avail.height() - frame));

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

  // The floor, applied last so nothing above can undo it. Note it is relative
  // to the WORK AREA's top, not the screen's: with a taskbar along the top,
  // avail.y() is already below it and the decoration needs room below that.
  y = qMax(y, avail.y() + frame);
  x = qMax(x, avail.x());
  if (y + h > avail.bottom() + 1) y = qMax(avail.y() + frame, avail.bottom() + 1 - h);
  if (x + w > avail.right() + 1) x = qMax(avail.x(), avail.right() + 1 - w);

  return QRect(x, y, w, h);
}
