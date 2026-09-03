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
#include <QEvent>
#include <QKeyEvent>
#include <QWindow>
#include <QVariantList>
#include <QStringList>
#include <QVariantMap>

#include "api_client.h"
#include "global_hotkey.h"
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

  // ── Recording, reflected from the HOST ──
  // ⚠️ The host has recorded and buffered since 08/30 and no panel could reach
  // it. These follow /api/record/status, not a local flag: the recorder can be
  // started by another client, and a button that latches on click would show
  // "recording" for a recorder that refused to start.
  Q_PROPERTY(bool recording READ recording NOTIFY recordChanged)
  Q_PROPERTY(bool recordAvailable READ recordAvailable NOTIFY recordChanged)
  Q_PROPERTY(QString recordStatus READ recordStatus NOTIFY recordChanged)

  // ── DSP, reflected from the rig ──
  Q_PROPERTY(QVariantMap dsp READ dsp NOTIFY statusFullChanged)
  Q_PROPERTY(QString agc READ agc NOTIFY statusFullChanged)

  // ── Audio / transmit ──
  Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioChanged)
  Q_PROPERTY(QString profileStatus READ profileStatus NOTIFY audioChanged)
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
  // ⚠️ SHOWN IN THE FOOTER ON iOS, because the thing that decides whether audio
  // survives a locked screen is invisible otherwise. There is no Mac or phone in
  // this development loop, so the operator's screenshot IS the measurement -
  // "PlayAndRecord active 48000 Hz" is what a working session looks like.
  // The phone shape. NOTIFY rides uiScaleChanged because both are decided by the
  // same thing - the screen the app is on - and setScreen already emits it.
  Q_PROPERTY(bool phoneLayout READ phoneLayout NOTIFY uiScaleChanged)
  Q_PROPERTY(QString audioSessionText READ audioSessionText CONSTANT)
  Q_PROPERTY(QString savedHost READ savedHost CONSTANT)
  Q_PROPERTY(int savedPort READ savedPort CONSTANT)
  Q_PROPERTY(QString savedUser READ savedUser CONSTANT)

  // ── Audio ──
  Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY audioChanged)
  Q_PROPERTY(int micGain READ micGain WRITE setMicGain NOTIFY audioChanged)
  Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY audioChanged)
  Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY audioChanged)
  Q_PROPERTY(int outputIndex READ outputIndex WRITE setOutputIndex NOTIFY audioChanged)
  Q_PROPERTY(int inputIndex READ inputIndex WRITE setInputIndex NOTIFY audioChanged)

  // ── Rig knobs, read from the rig, written by the operator ──
  Q_PROPERTY(int afGain READ afGain NOTIFY statusFullChanged)
  Q_PROPERTY(int rfGain READ rfGain NOTIFY statusFullChanged)
  Q_PROPERTY(int cwSpeed READ cwSpeed NOTIFY statusFullChanged)
  Q_PROPERTY(int ant READ ant NOTIFY statusFullChanged)
  Q_PROPERTY(long long freqB READ freqB NOTIFY statusFullChanged)
  Q_PROPERTY(QString bandName READ bandName NOTIFY statusChanged)
  // ⚠️ Two locks, and they are not the same thing: vfoLocked is THIS HOST's
  // software lock, which blocks frequency-changing routes for every caller;
  // rigLocked is the radio's own CAT lock. See WIP §6.
  Q_PROPERTY(bool vfoLocked READ vfoLocked NOTIFY statusChanged)
  Q_PROPERTY(bool rigLocked READ rigLocked NOTIFY statusFullChanged)
  Q_PROPERTY(bool diversity READ diversity NOTIFY statusChanged)
  Q_PROPERTY(QString preampName READ preampName NOTIFY statusFullChanged)
  // The tuning step the wheel and the ± keys use. Remembered.
  Q_PROPERTY(int stepHz READ stepHz WRITE setStepHz NOTIFY hotkeyChanged)
  Q_PROPERTY(QString stepLabel READ stepLabel NOTIFY hotkeyChanged)
  Q_PROPERTY(QString tunerStatus READ tunerStatus NOTIFY tunerChanged)
  Q_PROPERTY(bool tunerAvailable READ tunerAvailable NOTIFY tunerChanged)
  // ⚠️ FROM THE HOST'S STATUS, NOT FROM THE CLICK. Same rule as the PTT button
  // reflecting the rig's tx: the tune keys the transmitter, and a button that
  // latches on click would show "tuning" for a tune that never started - or,
  // worse, show idle while the carrier is still up. The host now returns
  // immediately and reports progress in tgxl_tuning, so this follows that.
  Q_PROPERTY(bool tunerActive READ tunerActive NOTIFY statusChanged)

  // ── Display ──
  // ⚠️ The panel is drawn at ONE scale, decided here rather than in the QML, so
  // there is a single number to reason about and a single place a test can pin.
  // See Theme.qml for why density and reflow are deliberately separate.
  Q_PROPERTY(qreal uiScale READ uiScale NOTIFY uiScaleChanged)
  Q_PROPERTY(QStringList uiScaleModes READ uiScaleModes CONSTANT)
  Q_PROPERTY(int uiScaleIndex READ uiScaleIndex WRITE setUiScaleIndex NOTIFY uiScaleChanged)
  Q_PROPERTY(QString displayInfo READ displayInfo NOTIFY uiScaleChanged)

  // ── Safe-area insets ──────────────────────────────────────────────────────
  // ⚠️ THE ONE PHONE PROBLEM --check-resolutions CANNOT SEE. It measures keys
  // against the WINDOW, and on a handset the window includes the notch, the
  // rounded corners and the home indicator - regions the operator cannot touch
  // and the system draws over. Every key can measure as present, on-screen and
  // large enough while the top row sits under the camera housing.
  //
  // ⚠️ CARRIED AS PLAIN PROPERTIES RATHER THAN QML's SafeArea ATTACHED TYPE,
  // which arrived in Qt 6.9. This project builds against 6.4 on Linux, so a
  // SafeArea binding would be a change nothing here could compile, let alone
  // test - and an untestable safety-shaped change is the exact thing that has
  // shipped broken in this repo before. As properties they are settable, so the
  // resolution walk can simulate a notch and the layout can be PROVEN to move.
  Q_PROPERTY(int safeTop    READ safeTop    NOTIFY safeAreaChanged)
  Q_PROPERTY(int safeBottom READ safeBottom NOTIFY safeAreaChanged)
  Q_PROPERTY(int safeLeft   READ safeLeft   NOTIFY safeAreaChanged)
  Q_PROPERTY(int safeRight  READ safeRight  NOTIFY safeAreaChanged)

  // ── Hotkey ──
  Q_PROPERTY(QVariantList hotkeyChoices READ hotkeyChoices CONSTANT)
  Q_PROPERTY(int hotkeyIndex READ hotkeyIndex WRITE setHotkeyIndex NOTIFY hotkeyChanged)
  Q_PROPERTY(bool hotkeyHold READ hotkeyHold WRITE setHotkeyHold NOTIFY hotkeyChanged)

  // ── System-wide PTT key ──
  // ⚠️ SEPARATE FROM THE WINDOW-FOCUS KEY, because they are not the same
  // capability. This one works while the logging program is in front; it is
  // press-to-TOGGLE only, because Windows' RegisterHotKey has no key-up. The
  // status says ARMED or exactly why it is not - a PTT key that silently does
  // nothing is worse than no PTT key.
  Q_PROPERTY(QStringList globalHotkeyChoices READ globalHotkeyChoices CONSTANT)
  Q_PROPERTY(int globalHotkeyIndex READ globalHotkeyIndex WRITE setGlobalHotkeyIndex
                 NOTIFY hotkeyChanged)
  Q_PROPERTY(QString globalHotkeyStatus READ globalHotkeyStatus NOTIFY hotkeyChanged)
  // ⚠️ A PRESS COUNT, because "not registered" and "registered but the press
  // never arrives" are different faults with different fixes and they look
  // identical from the operator's chair. The reference client tracks this for
  // the same reason.
  Q_PROPERTY(int globalHotkeyPresses READ globalHotkeyPresses NOTIFY hotkeyChanged)

 public:
  explicit Backend(QObject* parent = nullptr);

  Q_INVOKABLE bool connectTo(const QString& host, int port, const QString& user,
                             const QString& password);
  Q_INVOKABLE void send(const QString& path);      // fire a rig route
  Q_INVOKABLE void toggleArm();
  // ⚠️ KEYS ARE FILTERED AT THE APPLICATION, NOT HANDLED BY AN ITEM IN THE QML.
  // A `Keys.onPressed` handler only ever fires on the item that holds focus,
  // and the panel is full of things that take it: the connect screen's text
  // fields, a dropdown, a slider, the scroll area. The hotkey was dead in the
  // running application while its unit test passed, because the events never
  // arrived. An application-level filter sees the key whatever has focus.
  bool eventFilter(QObject* watched, QEvent* event) override;

  Q_INVOKABLE void keyPressed(int key, bool autoRepeat);
  Q_INVOKABLE void keyReleased(int key, bool autoRepeat);
  Q_INVOKABLE void focusLost();
  Q_INVOKABLE void saveGeometry(int x, int y, int w, int h);
  Q_INVOKABLE QVariantMap restoreGeometry(int availW, int availH);
  // --reset-window: forget a stored position that has left the window
  // unreachable. See restoreGeometry for how one gets there.
  void resetWindowGeometry();

  // The window tells us which screen it is on. ⚠️ It is called again when the
  // window is DRAGGED TO ANOTHER MONITOR, which is the case a scale computed
  // once at startup gets wrong - and a dual-monitor desk is the normal case,
  // not the exotic one.
  Q_INVOKABLE void setScreen(int availW, int availH, qreal dpr);

  int safeTop()    const { return safe_top_; }
  int safeBottom() const { return safe_bottom_; }
  int safeLeft()   const { return safe_left_; }
  int safeRight()  const { return safe_right_; }
  // Set from the platform on iOS, and by --check-resolutions to simulate one.
  Q_INVOKABLE void setSafeArea(int top, int bottom, int left, int right);

  void useTestTone() { tx_audio_.UseTestTone(true); }

  // ⚠️ DETERMINISTIC TEARDOWN. Sockets and timers must be stopped while the
  // application object is still alive; leaving it to destructor ordering across
  // Qt Network, WebSockets and Multimedia crashed on exit with heap corruption
  // in ~ApiClient. The Widgets front end did this in closeEvent and never saw
  // it; the QML one had no equivalent until now.
  Q_INVOKABLE void shutdown();
  Q_INVOKABLE void disconnectSession();
  Q_INVOKABLE void tuneTgxl();

  // ⚠️ TYPING A FREQUENCY. Returns "" on success, or the reason it refused -
  // and it DOES refuse rather than send something plausible: /api/freq/set
  // moves the MODE as well, so a misread number can take the rig out of the
  // mode it was working. See client/src/freq_input.h for the accepted forms.
  Q_INVOKABLE QString setFreqText(const QString& text);
  // Seeds the edit box with a value that parses back to the same frequency.
  Q_INVOKABLE QString freqEditText() const;

  int volume() const { return settings_.volume; }
  void setVolume(int v);
  int micGain() const { return tx_audio_.mic_gain(); }
  void setMicGain(int v);
  void PushProfile();
  QStringList outputDevices() const;
  QStringList inputDevices() const;
  int outputIndex() const;
  int inputIndex() const;
  void setOutputIndex(int i);
  void setInputIndex(int i);

  int afGain() const { return full_.value("af_gain").toInt(); }
  int rfGain() const { return full_.value("rf_gain").toInt(); }
  int cwSpeed() const { return cw_speed_; }
  int ant() const { return full_.value("ant").toInt(); }
  long long freqB() const { return full_.value("freq_b").toVariant().toLongLong(); }
  QString bandName() const;
  bool vfoLocked() const { return status_.value("vfo_locked").toBool(); }
  bool rigLocked() const { return full_.value("lock").toBool(); }
  bool diversity() const { return status_.value("diversity").toBool(); }
  QString preampName() const;
  int stepHz() const { return settings_.step_hz; }
  void setStepHz(int hz);
  QString stepLabel() const;
  QString tunerStatus() const { return tuner_status_; }
  bool tunerAvailable() const { return tuner_available_; }
  bool tunerActive() const { return status_.value("tgxl_tuning").toBool(); }

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

  bool recording() const { return record_.value("file_recording").toBool() ||
                                  record_.value("recording").toBool(); }
  bool recordAvailable() const { return record_.value("available").toBool(); }
  QString recordStatus() const;
  Q_INVOKABLE void toggleRecording();
  Q_INVOKABLE void saveReplay();

  QVariantMap dsp() const;
  QString agc() const { return full_.value("agc").toString("—"); }

  QString audioStatus() const { return audio_status_; }
  QString profileStatus() const { return profile_status_; }
  QString txStatus() const { return tx_audio_.StatusLine(); }
  bool armed() const { return tx_audio_.armed(); }
  bool testTone() const { return tx_audio_.using_test_tone(); }
  QString connectionText() const { return connection_text_; }

  bool sessionActive() const { return session_active_; }
  bool connecting() const { return connecting_; }
  QString lastError() const { return last_error_; }
  // ⚠️ THE SCHEME COMES BACK WITH IT. The connect screen prefills this field, and
  // a saved TLS target that redisplays as a bare hostname is one Connect press
  // away from silently reverting to http on the port box's number.
  bool phoneLayout() const;
  QString audioSessionText() const;
  QString savedHost() const {
    return settings_.tls && !settings_.host.isEmpty() ? "https://" + settings_.host
                                                      : settings_.host;
  }
  int savedPort() const { return settings_.port; }
  QString savedUser() const { return settings_.username; }

  qreal uiScale() const;
  QStringList uiScaleModes() const;
  int uiScaleIndex() const;
  void setUiScaleIndex(int i);
  QString displayInfo() const;
  // --ui-scale on the command line, for capturing the panel at a scale this
  // machine does not have a monitor for. Overrides the mode and says so.
  void setUiScaleOverride(qreal s);

  QVariantList hotkeyChoices() const;
  int hotkeyIndex() const { return hotkey_index_; }
  void setHotkeyIndex(int i);
  bool hotkeyHold() const { return settings_.ptt_hold; }
  void setHotkeyHold(bool hold);

  QStringList globalHotkeyChoices() const { return GlobalHotkey::Choices(); }
  int globalHotkeyIndex() const;
  void setGlobalHotkeyIndex(int i);
  QString globalHotkeyStatus() const { return global_hotkey_status_; }
  int globalHotkeyPresses() const { return global_hotkey_.pressCount(); }
  // Called once the window exists: RegisterHotKey needs a window handle.
  void attachWindow(QWindow* w);

 signals:
  void statusChanged();
  void statusFullChanged();
  void metersChanged();
  void scaleChanged();
  void audioChanged();
  void txChanged();
  void hotkeyChanged();
  void sessionChanged();
  void tunerChanged();
  void recordChanged();
  void uiScaleChanged();
  void safeAreaChanged();

 private:
  void ApplyGlobalHotkey();
  void refreshRecord();

  Settings settings_;
  ApiClient api_;
  RxAudio rx_;
  TxAudio tx_audio_;
  PttHotkey hotkey_;
  GlobalHotkey global_hotkey_;
  QString global_hotkey_status_ = "off";
  QWindow* window_ = nullptr;

  QJsonObject status_, full_, meters_, record_;
  QVariantList ticks_;
  QString audio_status_ = "idle";
  QString profile_status_ = "";
  QString connection_text_ = "not connected";
  int hotkey_index_ = 0;
  bool was_tx_ = false;
  int safe_top_ = 0, safe_bottom_ = 0, safe_left_ = 0, safe_right_ = 0;
  bool session_active_ = false;
  bool connecting_ = false;
  QString last_error_;
  QString tuner_status_ = "idle";
  bool tuner_available_ = false;
  int cw_speed_ = 0;

  // Display. avail_* are DEVICE-INDEPENDENT pixels: Qt has already divided out
  // the device pixel ratio, and multiplying it back in is the double-scaling
  // bug that draws a HiDPI panel at twice the intended size.
  int avail_w_ = 0;
  int avail_h_ = 0;
  qreal dpr_ = 1.0;
  qreal ui_scale_override_ = 0.0;   // 0 = not overridden
};
