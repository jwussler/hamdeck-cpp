#include "backend.h"

#include <QDateTime>

#ifdef Q_OS_IOS
#include "ios_audio_session.h"
#endif

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
      // ⚠️ Driven from the RIG's tx state, like everything else here, so the
      // drive meter speeds up for a hand mic at the radio too - not only for a
      // PTT pressed in this panel.
      api_.SetTxActive(tx);
      if (!tx) {
        // Off the air the last peak is not a reading of anything. Zero it rather
        // than leave a number sitting there looking live.
        tx_peak_ = 0;
        emit metersChanged();
      }
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
  connect(&api_, &ApiClient::BackendUpdated, this, [this](QJsonObject b) {
    tx_peak_ = b.value("tx_peak").toInt();
    rx_peak_ = b.value("rx_peak").toInt();

    // ⚠️ TWENTY SECONDS, AND ONLY WHILE CONNECTED AND NOT TRANSMITTING. Ten is
    // twitchy on a dead band between overs; thirty is long enough that the
    // operator has already asked out loud what is wrong. The receiver is muted
    // while keyed, so silence there means nothing at all.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (rx_peak_ > 0 || !session_active_ || tx()) {
      rx_quiet_since_ = 0;
      rx_silent_ = false;
    } else {
      if (rx_quiet_since_ == 0) rx_quiet_since_ = now;
      rx_silent_ = (now - rx_quiet_since_) >= 20000;
    }
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
    // ⚠️ HOLD SENDS ON; ONLY A TOGGLE HAS TO ASK THE RIG. Where the platform can
    // report a release, the key itself says which edge this is and there is no
    // state to be stale about. Where it cannot, this is a toggle and it goes
    // through the same fresh read as the on-screen button - it used to decide
    // from the polled tx flag, which is the bug that made the first press after
    // an idle gap do nothing.
    if (global_hotkey_.Mode() == "hold") {
      send("/api/ptt/on");
      // ⚠️ THE HOLD LIMIT, AND IT IS DELIBERATELY NOT CLEVER. A lost release and
      // a genuinely long over are identical from here: audio still streams, the
      // link is fine, the operator is simply talking. So this is longer than any
      // real over and SHORTER than the host's 180 s watchdog, and it exists only
      // so this app can say which safeguard fired. The watchdog next to the
      // radio remains the thing that protects the transmitter.
      hold_limit_.start();
    } else {
      togglePtt();
    }
    // So the panel can show that the press ARRIVED, even if the rig then
    // refuses. Without this an operator cannot tell a key Windows never
    // delivered from a command the host rejected.
    emit hotkeyChanged();
  });

  connect(&global_hotkey_, &GlobalHotkey::Released, this, [this] {
    hold_limit_.stop();
    if (global_hotkey_.Mode() == "hold") send("/api/ptt/off");
    emit hotkeyChanged();
  });

  // ⚠️ Says what happened, in the panel, rather than just unkeying. An operator
  // whose transmission stopped needs to know it was this and not the rig.
  hold_limit_.setSingleShot(true);
  hold_limit_.setInterval(150000);
  connect(&hold_limit_, &QTimer::timeout, this, [this] {
    send("/api/ptt/off");
    global_hotkey_status_ = "⚠ PTT key held past 150 s - unkeyed. If you were still "
                            "talking, the key's release was lost.";
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
    // ⚠️ THE KEY SAYS WHICH, so it does not need the read the button needs. A
    // hold sends on at key-down and off at key-up: there is no state to be stale
    // about. Only the on-screen toggle has to ask the rig what it is doing.
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

  // ⚠️ MIGRATION, SO NOBODY LOSES THE KEY THEY WERE USING. Before 09/04/2026
  // there were two settings: an in-app `ptt_key` and a separate
  // `global_ptt_key`, and an operator could be on F9 in one and F13 in the
  // other. There is one now. If the old global setting names a key in the list
  // and the in-app one was still the default, the GLOBAL choice wins - it is
  // the one that was working from anywhere, so it is the one they meant.
  if (!settings_.global_ptt_key.isEmpty() && settings_.global_ptt_key != "Off" &&
      settings_.ptt_key == Qt::Key_Pause) {
    if (const HotkeyChoice* g = PttHotkeyByLabel(settings_.global_ptt_key)) {
      const auto& all = PttHotkeyChoices();
      for (int i = 0; i < all.size(); ++i) {
        if (all[i].qt_key == g->qt_key) { hotkey_index_ = i; break; }
      }
      settings_.ptt_key = g->qt_key;
      settings_.Save();
    }
  }

  hotkey_.SetKey(PttHotkeyChoices()[hotkey_index_].qt_key);
  hotkey_.SetMode(settings_.ptt_hold ? PttMode::kHold : PttMode::kToggle);
  // Keep the stored global label in step with the one key, so a profile written
  // by this version cannot reintroduce the split.
  settings_.global_ptt_key = QString::fromLatin1(PttHotkeyChoices()[hotkey_index_].label);
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

// ⚠️ ONE SETTING, ARMED IN BOTH SCOPES. Choosing a PTT key now sets the
// in-window filter AND asks the platform for the system-wide registration, so
// there is no second list to disagree with this one. Where the global grab is
// refused - another program holds the key, or the platform has no code for it -
// globalHotkeyStatus says so; the key still works with the window focused.
void Backend::setHotkeyIndex(int i) {
  const auto& c = PttHotkeyChoices();
  if (i < 0 || i >= c.size()) return;
  hotkey_index_ = i;
  settings_.ptt_key = c[i].qt_key;
  hotkey_.SetKey(settings_.ptt_key);
  settings_.global_ptt_key = QString::fromLatin1(c[i].label);
  settings_.Save();
  ApplyGlobalHotkey();
  emit hotkeyChanged();
}

// The same index as the one PTT key. Kept so nothing that still reads it can
// disagree with the key actually armed.
int Backend::globalHotkeyIndex() const { return hotkey_index_; }

// ⚠️ THE SECOND CHOOSER IS GONE. It is routed to the one setting so that any
// caller left over - a saved profile, an older QML - cannot put the two scopes
// back on different keys.
void Backend::setGlobalHotkeyIndex(int i) { setHotkeyIndex(i); }

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
  // ⚠️ THE MODE IS NAMED, NEVER INFERRED. "Armed" alone lets an operator assume
  // hold-to-talk on a platform that can only toggle - and a PTT that silently
  // became a latch is a stuck transmitter waiting to happen. Windows holds now
  // (the hotkey gives the down edge, a poll of that ONE key gives the release);
  // macOS is focus-only until its release event is proven on real hardware.
  global_hotkey_status_ =
      err.isEmpty() ? QString("armed: %1 works anywhere · %2-to-talk")
                          .arg(settings_.global_ptt_key, global_hotkey_.Mode())
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
  // ⚠️ AN EMPTY PASSWORD NEVER REACHES THE HOST, and a failed login never takes
  // down a session that is already up. On a phone the keyboard stays over the
  // panel after Connect and the password has already been wiped, so a second
  // Return re-submitted an empty one; the 401 that came back tore up a working
  // session and threw the operator back to the login screen mid-over. Guarded in
  // the panel too - this is the half that holds when anything else calls it.
  if (password.isEmpty()) {
    last_error_ = "password required";
    emit sessionChanged();
    return false;
  }
  if (session_active_) return true;
  connecting_ = true;
  last_error_.clear();
  emit sessionChanged();

  // ⚠️ THE HOST FIELD IS PARSED, NOT TAKEN LITERALLY. An operator who types
  // `https://radio.wa0o.com` means TLS on 443, whatever the port box still says
  // from the last LAN session - see Settings::ParseTarget.
  const Settings::Target t = Settings::ParseTarget(host, port);
  if (t.host.isEmpty()) {
    connecting_ = false;
    last_error_ = "host and username are required";
    emit sessionChanged();
    return false;
  }
  settings_.host = t.host;
  settings_.port = t.port;
  settings_.tls = t.tls;
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
    rx_.Start(settings_.WsUrl("/ws", token), settings_.rx_device_name);
  }
  emit statusChanged();
  return true;
}

void Backend::send(const QString& path) { api_.Get(path); }

// ⚠️ THE FIRST TAP AFTER A GAP USED TO DO NOTHING, AND THIS IS WHY.
//
// The PTT button chose its command from the POLLED tx flag: transmitting means
// send /api/ptt/off, otherwise send on. After the app had been idle - backgrounded
// on a phone, a laptop asleep, a link that blinked - that flag was whatever the
// last poll had left behind, and if it said "transmitting" the first press sent
// an OFF to a radio that was not keyed. Nothing happened, the next poll corrected
// the flag, and the second press worked. "I have to double tap it."
//
// So the toggle reads the rig FIRST and acts on the answer. One round trip, and
// it cannot act on a belief older than the tap.
//
// ⚠️ AND WHEN THE READ FAILS IT DOES NOTHING AND SAYS SO. Guessing here means
// either a dead press or an unasked-for carrier; neither belongs on a
// transmitter. Same rule as the status route that must answer null rather than a
// plausible value.
void Backend::togglePtt() {
  api_.Get("/api/status", [this](QJsonObject s) {
    if (s.isEmpty()) {
      connection_text_ = "⚠ no reply from the host - PTT not sent";
      emit statusChanged();
      return;
    }
    status_ = s;
    const bool keyed = s.value("tx").toBool();
    api_.Get(keyed ? "/api/ptt/off" : "/api/ptt/on");
    emit statusChanged();
  });
}

// See the property's note in backend.h: on iOS this is the only visible evidence
// that background audio is configured. Empty everywhere else - a footer that
// said "n/a" on a desktop would be noise.
QString Backend::audioSessionText() const {
#ifdef Q_OS_IOS
  return ios_audio::State();
#else
  return QString();
#endif
}


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
  tx_audio_.Arm(settings_.WsUrl("/ws/tx", token), settings_.tx_device_name);

  // ⚠️ ARMING MUST PUT THE RADIO ON REAR/USB, AND NOTHING USED TO DO IT.
  //
  // The host hands the station back on every /ws/tx close: power to the local
  // cap and MOD SOURCE back to MIC, so the operator's own hand mic works when
  // they sit down at the radio. That is right, and it had no counterpart - this
  // client never called /api/remote-tx/on at all. So after ANY disconnect, a
  // dropped link and a backgrounded phone included, transmit was silently dead:
  // the rig keys, ALC sits at its idle floor, PWR stays 0, and every counter in
  // the audio chain reads perfectly healthy (CLAUDE.md, and hours went into it
  // once already). Three disconnects in one evening left it on MIC and the
  // operator reported "no audio is flowing again".
  //
  // ⚠️ AND IT REPORTS WHAT THE RADIO SAID, not what was sent. The route writes
  // and reads back in the same CAT task: verified true means the menu items were
  // read back as asked, false means they were not - and "unverified" is not the
  // same as "failed". A confident wrong answer here sends the search to the
  // wrong end of the chain, which is exactly what happened the first time.
  api_.Get("/api/remote-tx/on", [this](QJsonObject r) {
    const bool ok = r.value("status").toString() == "ok";
    const bool verified = r.value("verified").toBool();
    const bool rear = r.value("mod_source_rear").toBool();
    if (ok && verified && rear) {
      remote_tx_text_ = "radio on REAR/USB";
    } else if (ok && !verified) {
      remote_tx_text_ = "⚠ could not confirm the radio took REAR/USB - "
                        + r.value("message").toString();
    } else {
      remote_tx_text_ = "⚠ THE RADIO IS STILL ON MIC - transmit will key and "
                        "put out nothing: " + r.value("message").toString();
    }
    emit txChanged();
  });
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

// ⚠️ A PHONE IS NOT A SMALL DESKTOP, AND FITTING THE PANEL TO IT IS THE WRONG
// QUESTION. The automatic scale below fits the WHOLE panel - every group, top to
// bottom - into the screen, which on any handset lands on the 0.80 floor: a
// 30 px key on a 6-inch screen. That is what "really hard to work on a phone"
// measured out to.
//
// The phone layout shows ONE group at a time, so there is nothing to fit and the
// constraint is a thumb instead. Theme.keyH is 38 units, and Apple's touch
// target floor is 44 pt: 44/38 = 1.16, so 1.20 is the smallest scale that clears
// it with a little margin. It is a FLOOR, not a fixed value - a fixed density
// mode the operator picked still wins.
constexpr int  kPhoneWidth = 600;    // logical points, below which it is a handset
constexpr qreal kPhoneMinScale = 1.20;

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

// ⚠️ DECIDED HERE, NOT IN THE QML. The obvious QML test - "is the window
// narrower than 600 scaled units?" - is circular: Theme.u() is the scale, and
// the scale now depends on whether this is a phone. It reads fine and settles on
// whichever answer it happened to start from. The screen's logical width is the
// one input that does not move.
bool Backend::phoneLayout() const {
  return avail_w_ > 0 && avail_w_ < kPhoneWidth;
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
  if (phoneLayout()) return qBound(kPhoneMinScale, fit, kMaxScale);
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


// ⚠️ Insets are in LOGICAL pixels, the same units the layout uses. Feeding it
// physical pixels on a devicePixelRatio-3 phone would inset the panel by three
// times the notch and look like a layout bug rather than a units bug.
void Backend::setSafeArea(int top, int bottom, int left, int right) {
  // Negative margins would push content OFF the screen rather than away from
  // the edge - the opposite of the job.
  top = qMax(0, top); bottom = qMax(0, bottom);
  left = qMax(0, left); right = qMax(0, right);
  if (top == safe_top_ && bottom == safe_bottom_ &&
      left == safe_left_ && right == safe_right_) {
    return;
  }
  safe_top_ = top; safe_bottom_ = bottom; safe_left_ = left; safe_right_ = right;
  emit safeAreaChanged();
}

// ══ The drive test ══════════════════════════════════════════════════════════
//
// Two measurements, because "am I driving the radio hard enough" has two
// different answers and conflating them would give a confident wrong one.
//
// ⚠️ THE TONE SWEEP TRANSMITS. It keys the rig, walks the mic gain through a
// fixed ladder, and records what the RADIO reports at each step. The tone is a
// constant sine at 6000/32767 (18% of full scale), so the ladder is repeatable:
// run it twice and the two curves are comparable, which is what makes it a
// calibration rather than an impression.
//
// ⚠️ AND A TONE IS NOT A VOICE. A steady tone holds ALC where speech only touches
// it on peaks, so the gain that puts the TONE in the band is not the gain to
// speak at. The sweep answers "does the chain work, and over what gain range does
// this radio respond" - the voice check answers "are MY peaks landing in the
// band". Anyone who ships only the first and calls it calibration has measured
// the easy thing and reported the hard one.
//
// ⚠️ THE VOICE CHECK NEVER KEYS ANYTHING. The operator transmits the way they
// always do, on their own PTT, and this only watches. A calibration that keys the
// transmitter on its own while somebody is talking into it is not a measurement,
// it is a surprise.
//
// The sweep's guards, none of them optional:
//   - it refuses unless TX is armed and the rig is connected, with a different
//     message for each, because "it did nothing" is not a diagnosis;
//   - SWR at or above 3.0 aborts and unkeys immediately - that is the radio
//     telling us to stop, and it outranks the measurement;
//   - it only ever unkeys what it keyed, so a test started during somebody
//     else's over cannot drop their carrier;
//   - the operator's mic gain and tone setting are restored on EVERY exit path,
//     including an abort, because leaving a station on a test tone at 160% gain
//     is how the next over goes out splattering.

namespace {
// ⚠️ COARSE AND BOUNDED. Seven steps at ~0.9 s is about six seconds of carrier -
// long enough to read, short enough that the host's transmit watchdog is never
// the thing that ends it. A finer ladder would be a longer transmission for
// precision the ALC reading does not have.
const QVector<int> kSweepGains = {40, 60, 80, 100, 120, 140, 160};
constexpr int kTickMs = 300;
constexpr int kTicksPerStep = 3;      // set, settle, read
constexpr int kVoiceTicks = 34;       // ~10 s
constexpr double kSwrAbort = 3.0;
}  // namespace

void Backend::startToneSweep() {
  if (drive_mode_ != DriveMode::kOff) return;
  if (!session_active_) {
    drive_status_ = "not connected";
    emit driveTestChanged();
    return;
  }
  if (!tx_audio_.armed()) {
    // Arming is what claims the transmitter; without it the sweep would key a
    // rig with no audio path and measure a flat zero, which looks like a broken
    // radio rather than a missing step.
    drive_status_ = "arm TX first - the sweep needs the audio path open";
    emit driveTestChanged();
    return;
  }
  if (!status_.value("connected").toBool()) {
    drive_status_ = "the host has no radio";
    emit driveTestChanged();
    return;
  }

  drive_saved_gain_ = tx_audio_.mic_gain();
  drive_saved_tone_ = tx_audio_.using_test_tone();
  drive_rows_.clear();
  drive_result_.clear();
  drive_best_gain_ = 0;
  drive_gains_ = kSweepGains;
  drive_idx_ = 0;
  drive_tick_ = 0;
  drive_mode_ = DriveMode::kSweep;

  tx_audio_.UseTestTone(true);
  tx_audio_.SetMicGain(drive_gains_[0]);
  api_.Get("/api/ptt/on");
  drive_keyed_by_us_ = true;
  drive_status_ = QString("transmitting a test tone - step 1 of %1").arg(drive_gains_.size());

  drive_timer_.setInterval(kTickMs);
  drive_timer_.disconnect();
  connect(&drive_timer_, &QTimer::timeout, this, &Backend::DriveTick);
  drive_timer_.start();
  emit driveTestChanged();
}

void Backend::startVoiceCheck() {
  if (drive_mode_ != DriveMode::kOff) return;
  if (!session_active_) {
    drive_status_ = "not connected";
    emit driveTestChanged();
    return;
  }
  drive_rows_.clear();
  drive_result_.clear();
  drive_best_gain_ = 0;
  drive_peak_alc_ = 0;
  drive_peak_drive_ = 0;
  drive_tick_ = 0;
  drive_keyed_by_us_ = false;   // ⚠️ never, in this mode
  drive_mode_ = DriveMode::kVoice;
  drive_status_ = "key up and talk normally - watching your peaks";

  drive_timer_.setInterval(kTickMs);
  drive_timer_.disconnect();
  connect(&drive_timer_, &QTimer::timeout, this, &Backend::DriveTick);
  drive_timer_.start();
  emit driveTestChanged();
}

void Backend::DriveTick() {
  const double swr = meters_.value("swr_ratio").toDouble();
  const int alc = alcPct();
  const int pwr = powerPct();
  const int drv = txDrivePct();

  // ⚠️ THE RADIO'S OBJECTION OUTRANKS THE MEASUREMENT. Only while WE are keying:
  // an SWR reading taken with no carrier up is not about this test.
  if (drive_keyed_by_us_ && swr >= kSwrAbort) {
    EndDriveTest(QString("ABORTED - SWR reached %1:1").arg(swr, 0, 'f', 1), true);
    return;
  }

  if (drive_mode_ == DriveMode::kVoice) {
    if (status_.value("tx").toBool()) {
      drive_peak_alc_ = qMax(drive_peak_alc_, alc);
      drive_peak_drive_ = qMax(drive_peak_drive_, drv);
    }
    if (++drive_tick_ >= kVoiceTicks) {
      // ⚠️ PEAKS, NOT AVERAGES, and it says which it used. An average over an
      // over is dominated by the gaps between words - it would read low on a
      // perfectly driven station and send the operator to turn the gain up.
      QString verdict;
      if (drive_peak_drive_ == 0) {
        verdict = "NOTHING ARRIVED AT THE RADIO. The host saw no audio at all - "
                  "check that TX is armed and that the microphone is the one you think.";
      } else if (drive_peak_alc_ == 0) {
        verdict = QString("audio reached the radio (peak %1%) but ALC never moved. "
                          "The rig is not taking it as drive - check MOD SOURCE is "
                          "REAR and REAR SELECT is USB.").arg(drive_peak_drive_);
      } else if (drive_peak_alc_ > 90) {
        verdict = QString("peaks hit %1%% ALC - too hot. Come down until peaks sit "
                          "in the 50-75%% band.").arg(drive_peak_alc_);
      } else if (drive_peak_alc_ < 40) {
        verdict = QString("peaks only reached %1%% ALC - the radio is being under-driven. "
                          "Bring the gain up.").arg(drive_peak_alc_);
      } else {
        verdict = QString("peaks reached %1%% ALC. That is in the band the rig wants.")
                      .arg(drive_peak_alc_);
      }
      QVariantMap row;
      row["gain"] = tx_audio_.mic_gain();
      row["alc"] = drive_peak_alc_;
      row["pwr"] = pwr;
      row["drive"] = drive_peak_drive_;
      drive_rows_.append(row);
      EndDriveTest(verdict, false);
      return;
    }
    drive_status_ = QString("listening - %1 s left · peak ALC %2%")
                        .arg((kVoiceTicks - drive_tick_) * kTickMs / 1000)
                        .arg(drive_peak_alc_);
    emit driveTestChanged();
    return;
  }

  // ── The sweep ──────────────────────────────────────────────────────────────
  if (!status_.value("tx").toBool() && drive_tick_ > 1) {
    // Somebody or something dropped the carrier. Stop rather than carry on
    // recording zeros and calling them a curve.
    EndDriveTest("ABORTED - the rig stopped transmitting", true);
    return;
  }

  if (++drive_tick_ < kTicksPerStep) {
    emit driveTestChanged();
    return;
  }
  drive_tick_ = 0;

  QVariantMap row;
  row["gain"] = drive_gains_[drive_idx_];
  row["alc"] = alc;
  row["pwr"] = pwr;
  row["drive"] = drv;
  drive_rows_.append(row);

  if (++drive_idx_ >= drive_gains_.size()) {
    // ⚠️ THE LOWEST GAIN THAT REACHES THE BAND, not the one with the most ALC.
    // Past the band there is no more power to be had and the only thing still
    // rising is distortion - the curve is not monotonic in anything useful.
    int best = 0;
    for (const QVariant& v : std::as_const(drive_rows_)) {
      const QVariantMap m = v.toMap();
      const int a = m["alc"].toInt();
      if (a >= 50 && a <= 75) { best = m["gain"].toInt(); break; }
    }
    drive_best_gain_ = best;
    QString result;
    if (best > 0) {
      result = QString("%1%% gain put the tone at the ALC band. That is a TONE - "
                       "run the voice check before trusting it for speech.").arg(best);
    } else {
      int top = 0;
      for (const QVariant& v : std::as_const(drive_rows_)) top = qMax(top, v.toMap()["alc"].toInt());
      result = top == 0
        ? "No ALC at any gain. Nothing is reaching the radio as drive - check that "
          "MOD SOURCE is REAR and REAR SELECT is USB."
        : QString("Nothing landed in the 50-75%% band; the highest was %1%%. "
                  "The chain works but cannot drive the rig - check the codec's "
                  "output level on the host.").arg(top);
    }
    EndDriveTest(result, false);
    return;
  }

  tx_audio_.SetMicGain(drive_gains_[drive_idx_]);
  drive_status_ = QString("transmitting a test tone - step %1 of %2 · gain %3%")
                      .arg(drive_idx_ + 1).arg(drive_gains_.size()).arg(drive_gains_[drive_idx_]);
  emit driveTestChanged();
}

void Backend::stopDriveTest() {
  if (drive_mode_ == DriveMode::kOff) return;
  EndDriveTest("stopped", true);
}

// ⚠️ EVERY EXIT PATH COMES THROUGH HERE - finish, abort, SWR, the operator's
// stop. Restoring the gain on the success path only would leave a station on a
// test tone at 160% the one time it mattered, which is the time it went wrong.
void Backend::EndDriveTest(const QString& why, bool aborted) {
  drive_timer_.stop();
  const bool was_sweep = (drive_mode_ == DriveMode::kSweep);
  drive_mode_ = DriveMode::kOff;

  if (was_sweep) {
    // ⚠️ UNKEY, THEN CHECK IT ACTUALLY UNKEYED. Firing the request and moving on
    // is how a test run left a simulated rig transmitting: the call went out and
    // nothing ever looked at whether the carrier dropped. Guard the OUTCOME.
    // Two attempts, then say so loudly - a stuck PTT the operator has not been
    // told about is the worst thing this panel could leave behind. The host's
    // transmit watchdog is the backstop; it is not an excuse to skip this.
    if (drive_keyed_by_us_) UnkeyAndVerify(1);
    tx_audio_.SetMicGain(drive_saved_gain_);
    tx_audio_.UseTestTone(drive_saved_tone_);
    settings_.mic_gain = drive_saved_gain_;
    emit audioChanged();
  }
  drive_keyed_by_us_ = false;
  drive_status_ = aborted ? why : "done";
  if (!aborted || !drive_result_.isEmpty()) drive_result_ = why;
  else drive_result_ = why;
  emit driveTestChanged();
  emit txChanged();
}

// ⚠️ APPLYING IS THE OPERATOR'S ACT, NOT THE TEST'S. The sweep never sets the
// gain it found: it transmitted a tone, and the gain for a tone is not the gain
// for a voice. This is here so accepting the suggestion is one press once the
// operator has decided - and it goes through setMicGain, so it persists and
// reaches the host profile like any other gain change.
void Backend::applyBestGain() {
  if (drive_best_gain_ <= 0) return;
  setMicGain(drive_best_gain_);
}

void Backend::UnkeyAndVerify(int attempt) {
  api_.Get("/api/ptt/off");
  QTimer::singleShot(900, this, [this, attempt] {
    if (!status_.value("tx").toBool()) return;   // the carrier dropped: done
    if (attempt < 2) {
      UnkeyAndVerify(attempt + 1);
      return;
    }
    // ⚠️ SAY IT, DO NOT SWALLOW IT. The rig is still keyed after two attempts;
    // the operator needs to reach for the radio, not read "done".
    drive_status_ = "⚠ THE RIG IS STILL TRANSMITTING - unkey it at the radio";
    drive_result_ = drive_status_;
    emit driveTestChanged();
  });
}
