#include "backend.h"

#include <cstdio>

#include "freq_input.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>

#include "place.h"

Backend::Backend(QObject* parent) : QObject(parent) {
  settings_.Load();
  // ⚠️ APPLY it, not just load it. A setting that is saved and read back but
  // never handed to the thing it controls is the same as not saving it at all -
  // the panel would show the operator's gain while the audio path ran at 100%.
  tx_audio_.SetMicGain(settings_.mic_gain);

  connect(&api_, &ApiClient::StatusUpdated, this, [this](QJsonObject s) {
    status_ = s;
    const bool tx = s.value("tx").toBool();
    if (tx != was_tx_) {
      was_tx_ = tx;
      // ⚠️ Both driven from the RIG's tx state, never from a button. Any PTT
      // source - this panel, another client, the microphone - behaves alike.
      rx_.SetMutedForTx(tx);
      tx_audio_.SetKeyed(tx);
      emit txChanged();
    }
    emit statusChanged();
  });
  connect(&api_, &ApiClient::StatusFullUpdated, this, [this](QJsonObject f) {
    full_ = f;
    emit statusFullChanged();
  });
  connect(&api_, &ApiClient::MetersUpdated, this, [this](QJsonObject m) {
    meters_ = m;
    emit metersChanged();
  });
  connect(&api_, &ApiClient::ConnectionProblem, this, [this](QString why) {
    connection_text_ = "⚠ " + why;
    emit statusChanged();
  });

  // ⚠️ PRESS-TO-TOGGLE, and it asks the RIG what to do next rather than keeping
  // a flag of its own. If the rig is keyed - by this panel, another client, the
  // mic button or the tuner - the press unkeys it. A local "is it on" flag goes
  // out of step the first time anything else keys the radio.
  connect(&global_hotkey_, &GlobalHotkey::Pressed, this, [this] {
    send(tx() ? "/api/ptt/off" : "/api/ptt/on");
    // So the panel can show that the press ARRIVED, even if the rig then
    // refuses. Without this an operator cannot tell a key Windows never
    // delivered from a command the host rejected.
    emit hotkeyChanged();
  });

  connect(&rx_, &RxAudio::ConnectionChanged, this, [this](bool up, QString d) {
    audio_status_ = up ? "streaming" : d;
    emit audioChanged();
  });
  connect(&rx_, &RxAudio::BytesFlowing, this, [this](qint64, int bps) {
    // ⚠️ "arriving" and "playing" are separate facts. A stream that arrives but
    // is inaudible is a device problem; one that does not arrive is a link or
    // auth problem. One label for both sends people to the wrong fix.
    audio_status_ = QString("%1 KiB/s in%2")
                        .arg(bps / 1024.0, 0, 'f', 1)
                        .arg(rx_.CanPlay() ? "" : " · no playable device");
    emit audioChanged();
  });
  connect(&tx_audio_, &TxAudio::ArmedChanged, this, [this](bool, QString) { emit txChanged(); });
  connect(&tx_audio_, &TxAudio::Sent, this, [this](qint64, int) { emit txChanged(); });

  connect(&hotkey_, &PttHotkey::PttRequested, this, [this](bool on) {
    // One path to the transmitter, shared with the on-screen button.
    api_.Get(on ? "/api/ptt/on" : "/api/ptt/off");
  });

  const int idx = [&] {
    const auto& c = PttHotkeyChoices();
    for (int i = 0; i < c.size(); ++i) {
      if (c[i].qt_key == settings_.ptt_key) return i;
    }
    return 0;
  }();
  hotkey_index_ = idx;
  hotkey_.SetKey(PttHotkeyChoices()[idx].qt_key);
  hotkey_.SetMode(settings_.ptt_hold ? PttMode::kHold : PttMode::kToggle);
}

QString Backend::freqText() const {
  const qint64 hz = status_.value("freq").toVariant().toLongLong();
  if (hz <= 0) return "—.———.———";
  return QString("%1.%2.%3")
      .arg(hz / 1000000)
      .arg((hz % 1000000) / 1000, 3, 10, QChar('0'))
      .arg(hz % 1000, 3, 10, QChar('0'));
}

QString Backend::swr() const {
  if (!meters_.contains("swr_ratio")) return "—";
  // One decimal, not three: the calibration behind it is a hamlib default
  // measured on a different radio, and quoting it finer than it is known would
  // dress up an estimate as a measurement.
  return QString::number(meters_.value("swr_ratio").toDouble(), 'f', 1);
}

QVariantMap Backend::dsp() const {
  QVariantMap m;
  for (const QString& k : {"nb", "nr", "notch", "att", "vox", "comp", "mon", "rit",
                           "lock", "rxant"}) {
    m.insert(k, full_.value(k).toBool());
  }
  return m;
}

QVariantList Backend::hotkeyChoices() const {
  QVariantList out;
  for (const auto& c : PttHotkeyChoices()) {
    QVariantMap m;
    m.insert("label", c.label);
    m.insert("note", c.note);
    out.append(m);
  }
  return out;
}

void Backend::setHotkeyIndex(int i) {
  const auto& c = PttHotkeyChoices();
  if (i < 0 || i >= c.size()) return;
  hotkey_index_ = i;
  settings_.ptt_key = c[i].qt_key;
  hotkey_.SetKey(settings_.ptt_key);
  settings_.Save();
  emit hotkeyChanged();
}

int Backend::globalHotkeyIndex() const {
  const QStringList choices = GlobalHotkey::Choices();
  const int i = choices.indexOf(settings_.global_ptt_key);
  return i < 0 ? 0 : i;      // an unknown stored label falls back to "Off"
}

void Backend::setGlobalHotkeyIndex(int i) {
  const QStringList choices = GlobalHotkey::Choices();
  if (i < 0 || i >= choices.size()) return;
  settings_.global_ptt_key = choices.at(i);
  settings_.Save();
  ApplyGlobalHotkey();
  emit hotkeyChanged();
}

void Backend::attachWindow(QWindow* w) {
  window_ = w;
  ApplyGlobalHotkey();
  emit hotkeyChanged();
}

void Backend::ApplyGlobalHotkey() {
  if (settings_.global_ptt_key == "Off") {
    global_hotkey_.Apply("Off", window_);
    global_hotkey_status_ = "off";
    return;
  }
  // ⚠️ THE FAILURE IS THE STATUS. The commonest one - another program already
  // holds the combination - is not a fault in this app, and an operator staring
  // at a key that does nothing has no way to know that unless it is said.
  const QString err = global_hotkey_.Apply(settings_.global_ptt_key, window_);
  global_hotkey_status_ =
      err.isEmpty() ? QString("armed: %1 works anywhere").arg(settings_.global_ptt_key)
                    : err;
  if (!err.isEmpty()) {
    // ⚠️ Say it on the console too. On Windows the panel may be behind the
    // logging program when this fails, which is exactly when nobody sees a
    // line of grey text in a UI they are not looking at.
    std::fprintf(stderr, "global hotkey: %s\n", err.toUtf8().constData());
  }
}

void Backend::setHotkeyHold(bool hold) {
  settings_.ptt_hold = hold;
  hotkey_.SetMode(hold ? PttMode::kHold : PttMode::kToggle);
  settings_.Save();
  emit hotkeyChanged();
}

bool Backend::connectTo(const QString& host, int port, const QString& user,
                        const QString& password) {
  // ⚠️ Refuse an empty host here rather than letting it become a request to
  // "http://:5002". There is deliberately no default host to fall back on.
  if (host.trimmed().isEmpty() || user.trimmed().isEmpty()) {
    last_error_ = "host and username are required";
    emit sessionChanged();
    return false;
  }
  connecting_ = true;
  last_error_.clear();
  emit sessionChanged();

  settings_.host = host.trimmed();
  settings_.port = port;
  settings_.username = user.trimmed();
  api_.SetBaseUrl(settings_.BaseUrl());

  bool ok = false;
  QEventLoop loop;
  api_.Login(user, password, [&](bool success, QString message) {
    ok = success;
    // ⚠️ No "login failed:" prefix. The message already says what happened, and
    // for a host that was never reached the prefix contradicts it - "login
    // failed: no answer from ..." reads as a rejected password and sends the
    // operator to re-type one at a host that is not answering.
    connection_text_ = success ? settings_.BaseUrl() : message;
    loop.quit();
  });
  loop.exec();
  connecting_ = false;
  if (!ok) {
    // The host distinguishes bad credentials (401) from the lockout (429), and
    // that difference is passed straight through. A client that flattens both
    // to "login failed" leaves the operator retrying into a five-minute lockout.
    last_error_ = connection_text_;
    session_active_ = false;
    emit sessionChanged();
    emit statusChanged();
    return false;
  }
  session_active_ = true;
  emit sessionChanged();
  // ⚠️ The host and username are remembered; the PASSWORD never is. Losing it
  // costs a login; storing it costs a credential on disk.
  settings_.Save();

  // ── The operator's profile, from the host ─────────────────────────────────
  // ⚠️ ONLY when the host actually HAS one. `stored:false` means nothing is
  // saved for this user, and applying an empty profile then would wipe good
  // local settings - handing the operator a mic gain of 100% on a machine where
  // they had set it correctly. Absent means leave everything alone.
  //
  // ⚠️ Machine-specific settings are not in the profile at all (see
  // settings.h): host, port, username, audio device names and window geometry
  // stay local, because carrying a device name to another PC is how somebody
  // ends up armed against a microphone that is not there.
  api_.Get("/api/profile", [this](QJsonObject o) {
    if (!o.value("stored").toBool()) {
      // ⚠️ SEED IT from what this machine already has, rather than leaving the
      // operator with nothing stored until they happen to touch a control. The
      // first login from a machine that is already set up correctly is exactly
      // when the good values are known.
      profile_status_ = "no profile on the host - seeding from this machine";
      PushProfile();
      emit audioChanged();
      return;
    }
    const QJsonObject prof = o.value("profile").toObject();
    if (prof.isEmpty()) {
      profile_status_ = "profile on the host was empty";
      emit audioChanged();
      return;
    }
    settings_.ApplyProfileJson(
        QString::fromUtf8(QJsonDocument(prof).toJson(QJsonDocument::Compact)));
    settings_.Save();

    // ⚠️ APPLY it to the live objects, not just the struct. A profile that is
    // loaded and displayed but never handed to the audio path leaves the panel
    // showing a gain the transmitter is not using - which is worse than not
    // loading it, because it looks right.
    tx_audio_.SetMicGain(settings_.mic_gain);
    rx_.SetVolume(settings_.volume);
    profile_status_ = QString("profile loaded (mic gain %1%)").arg(settings_.mic_gain);
    emit audioChanged();
    emit hotkeyChanged();
    emit uiScaleChanged();
  });

  // The meter scale comes from the HOST, so the face follows the rig. Without
  // it the meter draws unlabelled ticks rather than inventing a calibration.
  api_.Get("/api/meters/scale", [this](QJsonObject scale) {
    ticks_.clear();
    for (const QJsonValue& v : scale.value("ticks").toArray()) {
      QVariantMap t;
      t.insert("raw", v.toObject().value("raw").toInt());
      t.insert("label", v.toObject().value("label").toString());
      ticks_.append(t);
    }
    emit scaleChanged();
  });

  // The knobs the status routes do not carry.
  api_.Get("/api/cw-speed/get", [this](QJsonObject o) {
    cw_speed_ = o.value("wpm").toInt();
    emit statusFullChanged();
  });
  refreshRecord();
  api_.Get("/api/tune/tgxl/status", [this](QJsonObject o) {
    tuner_available_ = o.value("available").toBool();
    tuner_status_ = tuner_available_ ? "ready" : "not configured";
    emit tunerChanged();
  });

  api_.StartPolling();
  rx_.SetVolume(settings_.volume);
  const QString token = api_.SessionToken();
  if (!token.isEmpty()) {
    // ⚠️ QWebSocket does not carry the REST cookie jar, which is why the host
    // accepts ?token= as well.
    rx_.Start(QString("ws://%1:%2/ws?token=%3").arg(host).arg(port).arg(token),
              settings_.rx_device_name);
  }
  emit statusChanged();
  return true;
}

void Backend::send(const QString& path) { api_.Get(path); }

void Backend::disconnectSession() {
  tx_audio_.Disarm();
  rx_.Stop();
  api_.StopPolling();
  api_.Logout();
  session_active_ = false;
  status_ = QJsonObject();
  full_ = QJsonObject();
  meters_ = QJsonObject();
  connection_text_ = "not connected";
  emit sessionChanged();
  emit statusChanged();
  emit statusFullChanged();
  emit metersChanged();
}

void Backend::shutdown() {
  static bool done = false;
  if (done) return;   // closing and app-exit can both reach here
  done = true;
  // Order matters: stop producing, then stop the transports, then the session.
  tx_audio_.Disarm();
  rx_.Stop();
  api_.StopPolling();
  api_.Logout();
}

void Backend::toggleArm() {
  if (tx_audio_.armed()) {
    tx_audio_.Disarm();
    emit txChanged();
    return;
  }
  const QString token = api_.SessionToken();
  if (token.isEmpty()) return;
  tx_audio_.Arm(QString("ws://%1:%2/ws/tx?token=%3")
                    .arg(settings_.host).arg(settings_.port).arg(token),
                settings_.tx_device_name);
}

// ⚠️ THE HOTKEY WAS DEAD IN THE RUNNING APP WHILE ITS UNIT TEST PASSED.
// tests_hotkey.cpp proves the state machine - hold, toggle, auto-repeat
// suppression, unkey on focus loss - and every case passes. None of it ran,
// because QML's `Keys.onPressed` fires only on the item that holds focus, and
// the connect screen's password field takes focus on startup and the panel's
// dropdowns, sliders and scroll area take it afterwards. Two green lights and
// nothing keying the transmitter.
//
// Filtering at the application catches the key whatever has focus. It consumes
// ONLY the configured PTT key - everything else is passed through untouched, so
// typing a frequency or a password is unaffected.
bool Backend::eventFilter(QObject* watched, QEvent* event) {
  const QEvent::Type t = event->type();
  if (t != QEvent::KeyPress && t != QEvent::KeyRelease) {
    return QObject::eventFilter(watched, event);
  }
  auto* ke = static_cast<QKeyEvent*>(event);
  const bool consumed = (t == QEvent::KeyPress) ? hotkey_.HandleKeyPress(ke)
                                                : hotkey_.HandleKeyRelease(ke);
  // Returning true stops delivery. Only ever true for the PTT key itself.
  return consumed ? true : QObject::eventFilter(watched, event);
}

void Backend::keyPressed(int key, bool autoRepeat) {
  QKeyEvent e(QEvent::KeyPress, key, Qt::NoModifier, QString(), autoRepeat);
  hotkey_.HandleKeyPress(&e);
}

void Backend::keyReleased(int key, bool autoRepeat) {
  QKeyEvent e(QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
  hotkey_.HandleKeyRelease(&e);
}

void Backend::focusLost() { hotkey_.FocusLost(); }

// ── Display scale ───────────────────────────────────────────────────────────
//
// ⚠️ THE PANEL WILL BE RUN ON SCREENS NOBODY HERE OWNS. The same window is a
// third of a 4K monitor, most of a 1366x768 laptop, and taller than a 1024x600
// shack netbook. Left alone, Qt draws every one of those at the same pixel
// sizes: unreadable across the room on the first, clipped on the last.
//
// Two knobs, deliberately separate (see Theme.qml):
//   - DENSITY, decided here as one number and applied through Theme.u()/f().
//   - REFLOW, decided in the QML against the width actually available.
//
// ⚠️ avail_w_/avail_h_ are DEVICE-INDEPENDENT pixels. Qt has already applied
// the device pixel ratio; folding dpr in again is the double-scaling bug that
// renders a HiDPI panel at twice the size asked for. dpr is carried only so the
// status line can SAY what it found - it is never a term in the scale.
namespace {
// The geometry the panel was laid out at. Not a guess: it is the size the
// window opens at with room for the status bar and a little desktop around it.
constexpr qreal kRefW = 1280.0;
constexpr qreal kRefH = 900.0;
// ⚠️ Clamped at both ends, and the LOW end matters more. Below about 0.8 the
// silkscreen legends stop being readable (Theme.f() also floors type at 9 px),
// and a panel whose keys cannot be read is worse than one that scrolls.
constexpr qreal kMinScale = 0.80;
constexpr qreal kMaxScale = 1.75;

struct ScaleMode { const char* label; qreal factor; };   // factor 0 = automatic
const ScaleMode kModes[] = {
    {"Auto", 0.0}, {"90%", 0.90}, {"100%", 1.00}, {"125%", 1.25}, {"150%", 1.50},
};
constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
}  // namespace

void Backend::setScreen(int availW, int availH, qreal dpr) {
  if (availW == avail_w_ && availH == avail_h_ && qFuzzyCompare(dpr, dpr_)) return;
  avail_w_ = availW;
  avail_h_ = availH;
  dpr_ = dpr > 0 ? dpr : 1.0;
  emit uiScaleChanged();
}

qreal Backend::uiScale() const {
  // A command-line override beats everything and is reported as such: a
  // screenshot taken at a scale the machine does not have must not be
  // mistakable for one taken at the scale it does.
  if (ui_scale_override_ > 0) return ui_scale_override_;
  const int idx = uiScaleIndex();
  if (idx > 0 && idx < kModeCount) return kModes[idx].factor;

  // Automatic: fit to the SMALLER axis. Fitting width alone on a 2560x1080
  // ultrawide would scale the panel up until it no longer fits vertically -
  // and the panel is much taller than it is wide.
  if (avail_w_ <= 0 || avail_h_ <= 0) return 1.0;
  const qreal fit = qMin(avail_w_ / kRefW, avail_h_ / kRefH);
  return qBound(kMinScale, fit, kMaxScale);
}

QStringList Backend::uiScaleModes() const {
  QStringList out;
  for (const auto& m : kModes) out << QString::fromUtf8(m.label);
  return out;
}

int Backend::uiScaleIndex() const {
  for (int i = 0; i < kModeCount; ++i) {
    if (settings_.ui_scale_mode == QString::fromUtf8(kModes[i].label)) return i;
  }
  return 0;   // Auto, and an unrecognised stored value falls back to it
}

void Backend::setUiScaleIndex(int i) {
  if (i < 0 || i >= kModeCount) return;
  settings_.ui_scale_mode = QString::fromUtf8(kModes[i].label);
  settings_.Save();
  emit uiScaleChanged();
}

void Backend::setUiScaleOverride(qreal s) {
  ui_scale_override_ = (s > 0) ? qBound(0.5, s, 3.0) : 0.0;
  emit uiScaleChanged();
}

QString Backend::displayInfo() const {
  // Says what was MEASURED and what was chosen, in that order. A scale with no
  // stated basis is indistinguishable from one somebody typed in.
  const QString screen = (avail_w_ > 0)
      ? QString("%1×%2 work area").arg(avail_w_).arg(avail_h_)
      : QString("screen unknown");
  const QString dpr = QString(" · dpr %1").arg(dpr_, 0, 'f', 2);
  QString basis;
  if (ui_scale_override_ > 0) basis = " · --ui-scale";
  else if (uiScaleIndex() > 0) basis = " · fixed";
  else basis = " · auto";
  return screen + dpr + basis + QString(" · ui %1×").arg(uiScale(), 0, 'f', 2);
}

void Backend::saveGeometry(int x, int y, int w, int h) {
  settings_.window_geometry = QRect(x, y, w, h);
  settings_.Save();
}

QVariantMap Backend::restoreGeometry(int availW, int availH) {
  // ⚠️ THE WORK AREA HAS AN ORIGIN, AND QML CANNOT TELL US WHAT IT IS.
  // Screen.desktopAvailableWidth/Height are sizes only, so a taskbar along the
  // TOP or the LEFT - and a second monitor, which starts at a virtual-desktop
  // offset - would have the window placed as if the work area began at 0,0.
  // The origin comes from QScreen here; the sizes QML passed are used only as a
  // fallback for a screen we cannot read.
  QRect avail(0, 0, availW, availH);
  if (const QScreen* s = QGuiApplication::primaryScreen()) {
    const QRect ag = s->availableGeometry();
    if (ag.width() > 0 && ag.height() > 0) avail = ag;
  }
  setScreen(avail.width(), avail.height(), dpr_);

  // All the arithmetic lives in PlaceWindow, which has its own test covering
  // the arrangements this box cannot make: top and left taskbars, an unplugged
  // monitor, a saved size larger than the screen. See client/tests_place.cpp.
  const QRect g = PlaceWindow(settings_.window_geometry, avail, uiScale());

  QVariantMap m;
  m.insert("x", g.x());
  m.insert("y", g.y());
  m.insert("width", g.width());
  m.insert("height", g.height());
  return m;
}

// Throw away a stored position. The rescue path for a window that has already
// been stranded off-screen by an older build - otherwise the only fix is to
// find and edit an INI file, which is not a thing to ask of somebody whose
// panel is currently unusable.
void Backend::resetWindowGeometry() {
  settings_.window_geometry = QRect();
  settings_.Save();
}

// ── Audio ───────────────────────────────────────────────────────────────────
void Backend::setVolume(int v) {
  settings_.volume = qBound(0, v, 100);
  rx_.SetVolume(settings_.volume);
  settings_.Save();
  emit audioChanged();
}

void Backend::setMicGain(int v) {
  tx_audio_.SetMicGain(v);
  // ⚠️ SAVE IT. Applying without persisting meant the value survived exactly as
  // long as the process did.
  settings_.mic_gain = tx_audio_.mic_gain();
  settings_.Save();
  PushProfile();
  emit audioChanged();
}

// ⚠️ Best effort, and deliberately quiet. The profile is a convenience; a host
// that will not store it must never stop the operator using the radio, and a
// failure here is not worth a dialog in front of somebody mid-over. It is
// reported in the status line and nowhere else.
void Backend::PushProfile() {
  if (!session_active_) return;
  api_.Post("/api/profile", settings_.ProfileJson().toUtf8(),
            [this](QJsonObject o) {
              profile_status_ = o.value("stored").toBool()
                                    ? QString("profile saved to the host")
                                    : QString("profile NOT saved: ") +
                                          o.value("message").toString();
              emit audioChanged();
            });
}

QStringList Backend::outputDevices() const {
  QStringList out{"System default"};
  for (const QAudioDevice& d : QMediaDevices::audioOutputs()) out << d.description();
  return out;
}

QStringList Backend::inputDevices() const {
  QStringList out{"System default"};
  for (const QAudioDevice& d : QMediaDevices::audioInputs()) out << d.description();
  return out;
}

// ⚠️ Index 0 is always "System default", and an unknown remembered device falls
// back to it rather than to whatever happens to be first in the list. On many
// machines the first input is a monitor loopback, and transmitting the desktop's
// own audio is a memorable mistake.
int Backend::outputIndex() const {
  const int i = outputDevices().indexOf(settings_.rx_device_name);
  return i > 0 ? i : 0;
}
int Backend::inputIndex() const {
  const int i = inputDevices().indexOf(settings_.tx_device_name);
  return i > 0 ? i : 0;
}

void Backend::setOutputIndex(int i) {
  const QStringList d = outputDevices();
  // ⚠️ Stored by NAME, never index. Indices shift when USB devices come and go,
  // which is what produced a dead microphone before.
  settings_.rx_device_name = (i > 0 && i < d.size()) ? d.at(i) : QString();
  settings_.Save();
  emit audioChanged();
}
void Backend::setInputIndex(int i) {
  const QStringList d = inputDevices();
  settings_.tx_device_name = (i > 0 && i < d.size()) ? d.at(i) : QString();
  settings_.Save();
  emit audioChanged();
}

// ── Band name, for the readout ──────────────────────────────────────────────
QString Backend::bandName() const {
  const long long hz = status_.value("freq").toVariant().toLongLong();
  struct B { long long lo, hi; const char* name; };
  // Amateur allocations, so the label is right rather than a rounded guess. Out
  // of band reports "—" rather than inventing the nearest one: a wrong band
  // label on a panel is worse than none.
  static const B kBands[] = {
      {1800000, 2000000, "160m"},   {3500000, 4000000, "80m"},
      {5330000, 5410000, "60m"},    {7000000, 7300000, "40m"},
      {10100000, 10150000, "30m"},  {14000000, 14350000, "20m"},
      {18068000, 18168000, "17m"},  {21000000, 21450000, "15m"},
      {24890000, 24990000, "12m"},  {28000000, 29700000, "10m"},
      {50000000, 54000000, "6m"},
  };
  for (const auto& b : kBands) {
    if (hz >= b.lo && hz <= b.hi) return b.name;
  }
  return "—";
}

QString Backend::preampName() const {
  // The rig reports 0/1/2; the panel shows what the front panel calls them.
  switch (full_.value("preamp").toInt()) {
    case 1: return "AMP1";
    case 2: return "AMP2";
    default: return "IPO";
  }
}

void Backend::setStepHz(int hz) {
  // The steps the reference panel offers. Anything else is ignored rather than
  // sent: /api/step takes the size in the path and a typo would move the rig
  // somewhere nobody asked for.
  static const QList<int> kSteps{10, 100, 1000, 5000, 10000};
  if (!kSteps.contains(hz)) return;
  settings_.step_hz = hz;
  settings_.Save();
  emit hotkeyChanged();
}

QString Backend::stepLabel() const {
  const int hz = settings_.step_hz;
  return hz >= 1000 ? QString("%1 kHz").arg(hz / 1000) : QString("%1 Hz").arg(hz);
}

QString Backend::recordStatus() const {
  if (!record_.value("available").toBool()) {
    const QString why = record_.value("reason").toString();
    return why.isEmpty() ? "recording unavailable" : why;
  }
  if (record_.value("file_recording").toBool()) {
    const int secs = record_.value("recorded_seconds").toInt();
    return QString("recording · %1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
  }
  // ⚠️ "buffering" and "recording" are DIFFERENT and the panel must say which.
  // The replay buffer is always running; a file recording is not. An operator
  // who thinks a file is being written when only the buffer is running loses
  // the over they meant to keep.
  return record_.value("buffering").toBool() ? "buffer running · replay ready" : "idle";
}

void Backend::toggleRecording() {
  api_.Get("/api/record/toggle", [this](QJsonObject) { refreshRecord(); });
}

void Backend::saveReplay() {
  api_.Get("/api/record/replay", [this](QJsonObject) { refreshRecord(); });
}

void Backend::refreshRecord() {
  api_.Get("/api/record/status", [this](QJsonObject o) {
    record_ = o;
    emit recordChanged();
  });
}

QString Backend::setFreqText(const QString& text) {
  const long long hz = FreqInput::Parse(text);
  // ⚠️ 0 IS A REFUSAL, NOT A FREQUENCY. Sending "the closest thing we could make
  // of it" is how a rig ends up somewhere nobody asked for - and because
  // /api/freq/set also sets the mode, a bad parse is not a harmless miss.
  if (hz == 0) return QString("cannot read \"%1\" as a frequency").arg(text.trimmed());
  if (!FreqInput::InRange(hz)) {
    return QString("%1 MHz is outside the rig's 0.030-75 MHz range")
        .arg(hz / 1000000.0, 0, 'f', 3);
  }
  send(QString("/api/freq/set/%1").arg(hz));
  return {};
}

QString Backend::freqEditText() const {
  return FreqInput::ToEditText(status_.value("freq").toVariant().toLongLong());
}

void Backend::tuneTgxl() {
  // ⚠️ THIS IS A TOGGLE, because the host's route is one: pressing it while a
  // tune is running STOPS it. That matters more than it sounds - the tune keys
  // the transmitter, and an operator who wants the carrier to end needs one
  // press, not a support call.
  api_.Get("/api/tune/tgxl", [this](QJsonObject r) {
    // The host's own message, passed through: "not configured", "no answer" and
    // "stopping the tune" are different things with different fixes.
    tuner_status_ = r.value("message").toString(
        r.value("status").toString() == "ok" ? "tuning" : "tuner error");
    tuner_available_ = r.value("available").toBool();
    emit tunerChanged();
  });
  // ⚠️ Deliberately no optimistic "tuning…" here. The host answers immediately
  // now, and whether a carrier is actually up is reported by tgxl_tuning in the
  // status poll. Showing "tuning" from the click would be the button lying
  // about the transmitter, which is the one thing this panel must never do.
}
