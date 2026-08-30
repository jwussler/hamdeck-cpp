#pragma once

// Signal meter.
//
// ⚠️ DELIBERATELY NOT LABELLED IN S-UNITS. The rig reports 0-255 and where S9
// falls on that scale is a per-radio calibration nobody here has measured. A
// meter with S1..S9..+60 painted on it looks authoritative and would be a
// fabrication - and a signal report is a thing operators pass on to other people.
//
// So: an honest bar with evenly spaced ticks and the raw value. The scale gets
// its labels when someone measures a real radio against a known source.

#include <QWidget>

class SMeter : public QWidget {
  Q_OBJECT

 public:
  explicit SMeter(QWidget* parent = nullptr);

  void SetValue(int raw_0_255);
  void SetTransmitting(bool tx) { tx_ = tx; update(); }

 protected:
  void paintEvent(QPaintEvent*) override;
  QSize sizeHint() const override { return {360, 58}; }

 private:
  int value_ = 0;
  int peak_ = 0;
  bool tx_ = false;
};
