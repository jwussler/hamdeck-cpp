#include "smeter.h"

#include <QLinearGradient>
#include <QPainter>

SMeter::SMeter(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(58);
}

void SMeter::SetValue(int raw) {
  value_ = qBound(0, raw, 255);
  // Peak hold: a bar that only shows the instantaneous value hides short peaks
  // entirely at a 250 ms poll rate, which is most of what a signal report is.
  peak_ = qMax(value_ - 2, qMax(peak_ - 3, value_));
  if (value_ > peak_) peak_ = value_;
  update();
}

void SMeter::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF body(0, 0, width(), height());
  p.fillRect(body, QColor("#0d0f12"));
  p.setPen(QPen(QColor("#2a2f37"), 1));
  p.drawRect(body.adjusted(0.5, 0.5, -0.5, -0.5));

  const qreal pad = 8.0;
  // Room for ticks under the bar AND a caption line under those, so nothing
  // overlaps at small heights.
  const QRectF track(pad, 8, width() - 2 * pad, height() - 34);

  // Ticks. Evenly spaced and unlabelled - see the header. They give the eye
  // something to judge relative movement against without claiming a calibration.
  p.setPen(QPen(QColor("#4a5563"), 1));
  for (int i = 0; i <= 10; ++i) {
    const qreal x = track.left() + track.width() * i / 10.0;
    const qreal h = (i % 5 == 0) ? 6.0 : 3.0;
    p.drawLine(QPointF(x, track.bottom() + 2), QPointF(x, track.bottom() + 2 + h));
  }

  p.fillRect(track, QColor("#15181d"));

  const qreal frac = value_ / 255.0;
  if (frac > 0.0) {
    QLinearGradient g(track.topLeft(), track.topRight());
    if (tx_) {
      g.setColorAt(0.0, QColor("#c62828"));
      g.setColorAt(1.0, QColor("#ff7043"));
    } else {
      g.setColorAt(0.0, QColor("#2e7d32"));
      g.setColorAt(0.7, QColor("#9ccc65"));
      g.setColorAt(1.0, QColor("#ffb300"));
    }
    QRectF fill = track;
    fill.setWidth(track.width() * frac);
    p.fillRect(fill, g);
  }

  if (peak_ > 0) {
    const qreal x = track.left() + track.width() * (peak_ / 255.0);
    p.setPen(QPen(QColor("#ffffff"), 2));
    p.drawLine(QPointF(x, track.top()), QPointF(x, track.bottom()));
  }

  // The raw number, said plainly, because that is what we actually know.
  p.setPen(QColor("#7c8794"));
  QFont f = p.font();
  f.setPointSize(8);
  p.setFont(f);
  p.drawText(body.adjusted(6, 0, 0, -1), Qt::AlignLeft | Qt::AlignBottom, "SIGNAL");
  p.drawText(body.adjusted(0, 0, -6, -1), Qt::AlignRight | Qt::AlignBottom,
             QString("raw %1 / 255 — uncalibrated").arg(value_));
}
