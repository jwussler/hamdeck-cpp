#pragma once

#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QPushButton>
#include <QSlider>

#include "api_client.h"
#include "rx_audio.h"
#include "smeter.h"
#include "settings.h"
#include "tx_audio.h"

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool ConnectTo(const QString& host, int port, const QString& user,
                 const QString& password, QString* error);

  // Test facility: transmit a synthetic tone instead of the microphone, so the
  // path can be proven on a machine with no audio input. Named loudly and shown
  // in the UI, because a test tone mistaken for live audio would go on the air.
  void UseTxTestTone() { tx_audio_.UseTestTone(true); }
  void ArmTransmit();

  // Screenshot mode only: grow the window to the panel's natural height so the
  // whole app is visible in one image. NOT used in normal operation - the
  // work-area clamp exists precisely to stop the window doing this on a real
  // desktop, where it would put the title bar out of reach.
  void ResizeToContentForCapture();

 protected:
  void closeEvent(QCloseEvent* e) override;

 private:
  QWidget* BuildPanel();
  void ApplyStatus(const QJsonObject& s);
  void ApplyStatusFull(const QJsonObject& f);
  void ApplyMeters(const QJsonObject& m);
  void SetStale(bool stale, const QString& detail);

  // ⚠️ Clamp to the WORK AREA and re-centre. A window taller than the display
  // puts its title bar off-screen and the app cannot be reached at all - not
  // moved, not resized, not closed. Restoring saved geometry unchecked is how
  // that happens after someone unplugs a monitor (CARRYOVER.md section 6).
  void RestoreGeometryClamped();

  Settings settings_;
  ApiClient api_;
  RxAudio rx_;
  TxAudio tx_audio_;

  QLabel* freq_label_ = nullptr;
  QLabel* mode_label_ = nullptr;
  QLabel* vfo_label_ = nullptr;
  QLabel* power_label_ = nullptr;
  QLabel* conn_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  SMeter* smeter_ = nullptr;
  QPushButton* ptt_button_ = nullptr;
  QSlider* volume_ = nullptr;
  QLabel* swr_label_ = nullptr;
  QLabel* alc_label_ = nullptr;
  QLabel* pwr_label_ = nullptr;
  QLabel* buffer_label_ = nullptr;
  // DSP toggles, keyed by the /api/status/full field they reflect. They show the
  // RIG's state, never their own click history - a button that latches on click
  // lies whenever the command fails or another client changes it.
  QMap<QString, QPushButton*> dsp_;
  QPushButton* agc_button_ = nullptr;
  QPushButton* arm_button_ = nullptr;
  QLabel* tx_label_ = nullptr;
  bool tx_ = false;
  bool stale_ = false;
};
