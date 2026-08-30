#include "backend.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QRect>

Backend::Backend(QObject* parent) : QObject(parent) {
  settings_.Load();

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

void Backend::setHotkeyHold(bool hold) {
  settings_.ptt_hold = hold;
  hotkey_.SetMode(hold ? PttMode::kHold : PttMode::kToggle);
  settings_.Save();
  emit hotkeyChanged();
}

bool Backend::connectTo(const QString& host, int port, const QString& user,
                        const QString& password) {
  settings_.host = host;
  settings_.port = port;
  settings_.username = user;
  api_.SetBaseUrl(settings_.BaseUrl());

  bool ok = false;
  QEventLoop loop;
  api_.Login(user, password, [&](bool success, QString message) {
    ok = success;
    connection_text_ = success ? settings_.BaseUrl() : ("login failed: " + message);
    loop.quit();
  });
  loop.exec();
  if (!ok) {
    emit statusChanged();
    return false;
  }
  settings_.Save();

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

void Backend::keyPressed(int key, bool autoRepeat) {
  QKeyEvent e(QEvent::KeyPress, key, Qt::NoModifier, QString(), autoRepeat);
  hotkey_.HandleKeyPress(&e);
}

void Backend::keyReleased(int key, bool autoRepeat) {
  QKeyEvent e(QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
  hotkey_.HandleKeyRelease(&e);
}

void Backend::focusLost() { hotkey_.FocusLost(); }

void Backend::saveGeometry(int x, int y, int w, int h) {
  settings_.window_geometry = QRect(x, y, w, h);
  settings_.Save();
}

QVariantMap Backend::restoreGeometry(int availW, int availH) {
  // ⚠️ Clamp to the work area and re-centre if it would land outside. A window
  // taller than the display puts its title bar out of reach and the app cannot
  // be closed, moved or resized. Saved geometry is never trusted as written.
  QRect g = settings_.window_geometry;
  if (!g.isValid() || g.width() < 320 || g.height() < 240) {
    g = QRect(0, 0, qMin(880, availW), qMin(760, availH));
  }
  g.setWidth(qMin(g.width(), availW));
  g.setHeight(qMin(g.height(), availH));
  QVariantMap m;
  const bool fits = g.x() >= 0 && g.y() >= 0 &&
                    g.x() + g.width() <= availW && g.y() + g.height() <= availH;
  m.insert("x", fits ? g.x() : (availW - g.width()) / 2);
  m.insert("y", fits ? g.y() : (availH - g.height()) / 2);
  m.insert("width", g.width());
  m.insert("height", g.height());
  return m;
}
