#pragma once

// The QML front end's view of the host.
//
// ⚠️ THIS IS A FACADE, NOT LOGIC. Every rule that matters - the PTT button
// reflecting the RIG rather than the button, muting RX from the rig's tx state,
// counting down the host's watchdog, refusing to label an uncalibrated meter -
// lives in the C++ classes underneath and is shared with any other front end.
// A UI that reimplements them is a UI that can disagree with them.

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QKeyEvent>
#include <QVariantList>
#include <QVariantMap>

#include "api_client.h"
#include "ptt_hotkey.h"
#include "rx_audio.h"
#include "settings.h"
#include "tx_audio.h"

class Backend : public QObject {
  Q_OBJECT

  // ── Readout ──
  Q_PROPERTY(QString freqText READ freqText NOTIFY statusChanged)
  Q_PROPERTY(QString mode READ mode NOTIFY statusChanged)
  Q_PROPERTY(QString vfo READ vfo NOTIFY statusChanged)
  Q_PROPERTY(int power READ power NOTIFY statusChanged)
  Q_PROPERTY(bool tx READ tx NOTIFY statusChanged)
  Q_PROPERTY(bool stale READ stale NOTIFY statusChanged)
  Q_PROPERTY(int cacheAgeMs READ cacheAgeMs NOTIFY statusChanged)
  Q_PROPERTY(int txTimeoutIn READ txTimeoutIn NOTIFY statusChanged)
  Q_PROPERTY(QString freqBuffer READ freqBuffer NOTIFY statusChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)

  // ── Meters ──
  Q_PROPERTY(int sMeterRaw READ sMeterRaw NOTIFY metersChanged)
  Q_PROPERTY(QString sUnit READ sUnit NOTIFY metersChanged)
  Q_PROPERTY(QString swr READ swr NOTIFY metersChanged)
  Q_PROPERTY(int alcPct READ alcPct NOTIFY metersChanged)
  Q_PROPERTY(int powerPct READ powerPct NOTIFY metersChanged)
  // Tick marks for the meter face, supplied by the HOST so the scale follows the
  // rig rather than being hard-coded per client.
  Q_PROPERTY(QVariantList meterTicks READ meterTicks NOTIFY scaleChanged)

  // ── DSP, reflected from the rig ──
  Q_PROPERTY(QVariantMap dsp READ dsp NOTIFY statusFullChanged)
  Q_PROPERTY(QString agc READ agc NOTIFY statusFullChanged)

  // ── Audio / transmit ──
  Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioChanged)
  Q_PROPERTY(QString txStatus READ txStatus NOTIFY txChanged)
  Q_PROPERTY(bool armed READ armed NOTIFY txChanged)
  Q_PROPERTY(bool testTone READ testTone CONSTANT)
  Q_PROPERTY(QString connectionText READ connectionText NOTIFY statusChanged)

  // ── Session ──
  // ⚠️ sessionActive is about having LOGGED IN, which is not the same as the
  // rig being reachable. A host can answer perfectly while the radio is off, and
  // the panel must be able to say which of those is wrong.
  Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionChanged)
  Q_PROPERTY(bool connecting READ connecting NOTIFY sessionChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY sessionChanged)
  Q_PROPERTY(QString savedHost READ savedHost CONSTANT)
  Q_PROPERTY(int savedPort READ savedPort CONSTANT)
  Q_PROPERTY(QString savedUser READ savedUser CONSTANT)

  // ── Hotkey ──
  Q_PROPERTY(QVariantList hotkeyChoices READ hotkeyChoices CONSTANT)
  Q_PROPERTY(int hotkeyIndex READ hotkeyIndex WRITE setHotkeyIndex NOTIFY hotkeyChanged)
  Q_PROPERTY(bool hotkeyHold READ hotkeyHold WRITE setHotkeyHold NOTIFY hotkeyChanged)

 public:
  explicit Backend(QObject* parent = nullptr);

  Q_INVOKABLE bool connectTo(const QString& host, int port, const QString& user,
                             const QString& password);
  Q_INVOKABLE void send(const QString& path);      // fire a rig route
  Q_INVOKABLE void toggleArm();
  Q_INVOKABLE void keyPressed(int key, bool autoRepeat);
  Q_INVOKABLE void keyReleased(int key, bool autoRepeat);
  Q_INVOKABLE void focusLost();
  Q_INVOKABLE void saveGeometry(int x, int y, int w, int h);
  Q_INVOKABLE QVariantMap restoreGeometry(int availW, int availH);

  void useTestTone() { tx_audio_.UseTestTone(true); }

  // ⚠️ DETERMINISTIC TEARDOWN. Sockets and timers must be stopped while the
  // application object is still alive; leaving it to destructor ordering across
  // Qt Network, WebSockets and Multimedia crashed on exit with heap corruption
  // in ~ApiClient. The Widgets front end did this in closeEvent and never saw
  // it; the QML one had no equivalent until now.
  Q_INVOKABLE void shutdown();
  Q_INVOKABLE void disconnectSession();

  QString freqText() const;
  QString mode() const { return status_.value("mode").toString("—"); }
  QString vfo() const { return status_.value("vfo").toString("—"); }
  int power() const { return status_.value("power").toInt(); }
  bool tx() const { return status_.value("tx").toBool(); }
  bool stale() const { return status_.value("stale").toBool(); }
  int cacheAgeMs() const { return status_.value("cache_age_ms").toInt(); }
  int txTimeoutIn() const { return status_.value("tx_timeout_in").toInt(); }
  QString freqBuffer() const { return status_.value("freq_buffer").toString(); }
  bool connected() const { return status_.value("connected").toBool(); }

  int sMeterRaw() const { return meters_.value("s_meter").toInt(); }
  QString sUnit() const { return meters_.value("s_unit").toString(); }
  QString swr() const;
  int alcPct() const { return meters_.value("alc_pct").toInt(); }
  int powerPct() const { return meters_.value("power_pct").toInt(); }
  QVariantList meterTicks() const { return ticks_; }

  QVariantMap dsp() const;
  QString agc() const { return full_.value("agc").toString("—"); }

  QString audioStatus() const { return audio_status_; }
  QString txStatus() const { return tx_audio_.StatusLine(); }
  bool armed() const { return tx_audio_.armed(); }
  bool testTone() const { return tx_audio_.using_test_tone(); }
  QString connectionText() const { return connection_text_; }

  bool sessionActive() const { return session_active_; }
  bool connecting() const { return connecting_; }
  QString lastError() const { return last_error_; }
  QString savedHost() const { return settings_.host; }
  int savedPort() const { return settings_.port; }
  QString savedUser() const { return settings_.username; }

  QVariantList hotkeyChoices() const;
  int hotkeyIndex() const { return hotkey_index_; }
  void setHotkeyIndex(int i);
  bool hotkeyHold() const { return settings_.ptt_hold; }
  void setHotkeyHold(bool hold);

 signals:
  void statusChanged();
  void statusFullChanged();
  void metersChanged();
  void scaleChanged();
  void audioChanged();
  void txChanged();
  void hotkeyChanged();
  void sessionChanged();

 private:
  Settings settings_;
  ApiClient api_;
  RxAudio rx_;
  TxAudio tx_audio_;
  PttHotkey hotkey_;

  QJsonObject status_, full_, meters_;
  QVariantList ticks_;
  QString audio_status_ = "idle";
  QString connection_text_ = "not connected";
  int hotkey_index_ = 0;
  bool was_tx_ = false;
  bool session_active_ = false;
  bool connecting_ = false;
  QString last_error_;
};
