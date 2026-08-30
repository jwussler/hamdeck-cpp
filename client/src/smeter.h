#pragma once

// Signal meter.
//
// ⚠️ THE SCALE COMES FROM THE HOST, NOT FROM THIS FILE.
//
// The rig reports 0-255 and where S9 falls on it is a per-radio calibration.
// Hard-coding a table here would mean every client carrying its own copy for a
// radio it might not be talking to. The host serves /api/meters/scale, so
// swapping the rig moves every client's scale without shipping a new client.
//
// Until that arrives the meter draws UNLABELLED ticks and says "uncalibrated" -
// painting S1..S9..+60 on a scale we have not been given would look
// authoritative and be a fabrication, and a signal report is a thing operators
// pass on to other people.
//
// The host's numbers come from Hamlib's FTDX101D table, which is contributed by
// people with real radios. Better than an assumption; still not a measurement of
// THIS station.

#include <QString>
#include <QVector>
#include <QWidget>

class SMeter : public QWidget {
  Q_OBJECT

 public:
  explicit SMeter(QWidget* parent = nullptr);

  void SetValue(int raw_0_255);
  void SetUnitLabel(const QString& s_unit) { unit_ = s_unit; update(); }

  struct Tick { int raw; QString label; };
  void SetScale(const QVector<Tick>& ticks) { ticks_ = ticks; update(); }
  void SetTransmitting(bool tx) { tx_ = tx; update(); }

 protected:
  void paintEvent(QPaintEvent*) override;
  QSize sizeHint() const override { return {360, 64}; }

 private:
  QVector<Tick> ticks_;
  QString unit_;
  int value_ = 0;
  int peak_ = 0;
  bool tx_ = false;
};
