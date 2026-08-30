#include "smeter.h"

#include <QLinearGradient>
#include <QPainter>

SMeter::SMeter(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(64);
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

  // Ticks. Labelled at the host-supplied calibration points when we have them,
  // evenly spaced and unlabelled when we do not - never labelled from a guess.
  QFont tickfont = p.font();
  tickfont.setPointSize(7);
  p.setFont(tickfont);
  if (ticks_.isEmpty()) {
    p.setPen(QPen(QColor("#4a5563"), 1));
    for (int i = 0; i <= 10; ++i) {
      const qreal x = track.left() + track.width() * i / 10.0;
      const qreal h = (i % 5 == 0) ? 6.0 : 3.0;
      p.drawLine(QPointF(x, track.bottom() + 2), QPointF(x, track.bottom() + 2 + h));
    }
  } else {
    for (const Tick& t : ticks_) {
      const qreal x = track.left() + track.width() * (t.raw / 255.0);
      const bool over_s9 = t.label.startsWith('+');
      p.setPen(QPen(QColor(over_s9 ? "#c0392b" : "#4a5563"), 1));
      p.drawLine(QPointF(x, track.bottom() + 2), QPointF(x, track.bottom() + 7));
      p.setPen(QColor(over_s9 ? "#e07a6a" : "#8b97a5"));
      // Clamp the label box inside the widget. The last tick sits at the far
      // right, so a centred box runs off the edge and the label is clipped -
      // which is exactly the one an operator most wants to read.
      qreal lx = x - 16;
      if (lx < 0) lx = 0;
      if (lx + 32 > width()) lx = width() - 32;
      p.drawText(QRectF(lx, track.bottom() + 7, 32, 11),
                 Qt::AlignHCenter | Qt::AlignTop, t.label);
    }
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
  // The reading, said as an operator would say it - but only when the host has
  // given us a calibration. Otherwise the raw number and an explicit
  // "uncalibrated", so nobody reads an S-unit off a scale we invented.
  p.setPen(QColor("#8b97a5"));
  p.drawText(body.adjusted(6, 0, 0, -1), Qt::AlignLeft | Qt::AlignBottom, "SIGNAL");
  if (!unit_.isEmpty()) {
    QFont uf = p.font();
    uf.setPointSize(10);
    uf.setBold(true);
    p.setFont(uf);
    p.setPen(QColor(tx_ ? "#ff8a75" : "#ffb000"));
    p.drawText(body.adjusted(0, 0, -8, -1), Qt::AlignRight | Qt::AlignBottom, unit_);
  } else {
    p.drawText(body.adjusted(0, 0, -6, -1), Qt::AlignRight | Qt::AlignBottom,
               QString("raw %1 / 255 — uncalibrated").arg(value_));
  }
}
