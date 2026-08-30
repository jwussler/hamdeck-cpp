#pragma once

#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>

#include "api_client.h"
#include "rx_audio.h"
#include "smeter.h"
#include "settings.h"

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool ConnectTo(const QString& host, int port, const QString& user,
                 const QString& password, QString* error);

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

  QLabel* freq_label_ = nullptr;
  QLabel* mode_label_ = nullptr;
  QLabel* vfo_label_ = nullptr;
  QLabel* power_label_ = nullptr;
  QLabel* conn_label_ = nullptr;
  QLabel* audio_label_ = nullptr;
  SMeter* smeter_ = nullptr;
  QPushButton* ptt_button_ = nullptr;
  QSlider* volume_ = nullptr;
  bool tx_ = false;
  bool stale_ = false;
};
