#include "settings.h"
#include <QJsonObject>
#include <QJsonDocument>

#include <QSettings>
#include <QStandardPaths>

namespace {
// Organisation/app names decide the config path. They are deliberately generic:
// nothing here names a station.
constexpr const char* kOrg = "HamDeck";
constexpr const char* kApp = "HamDeckClient";
}  // namespace

void Settings::Load() {
  QSettings s(QSettings::IniFormat, QSettings::UserScope, kOrg, kApp);
  host = s.value("host", "").toString();
  port = s.value("port", 5002).toInt();
  tls  = s.value("tls", false).toBool();
  username = s.value("username", "").toString();
  rx_device_name = s.value("rx_device_name", "").toString();
  tx_device_name = s.value("tx_device_name", "").toString();
  volume = s.value("volume", 80).toInt();
  mic_gain = s.value("mic_gain", 100).toInt();
  ptt_key = s.value("ptt_key", static_cast<int>(Qt::Key_Pause)).toInt();
  ptt_hold = s.value("ptt_hold", true).toBool();
  ui_scale_mode = s.value("ui_scale_mode", "Auto").toString();
  global_ptt_key = s.value("global_ptt_key", "Off").toString();
  step_hz = s.value("step_hz", 1000).toInt();
  window_geometry = s.value("window_geometry", QRect()).toRect();

  // A password key must never exist. If an older build wrote one, remove it
  // rather than leave a credential lying in a config file.
  if (s.contains("password")) s.remove("password");
}

void Settings::Save() const {
  QSettings s(QSettings::IniFormat, QSettings::UserScope, kOrg, kApp);
  s.setValue("host", host);
  s.setValue("port", port);
  s.setValue("tls", tls);
  s.setValue("username", username);
  s.setValue("rx_device_name", rx_device_name);
  s.setValue("tx_device_name", tx_device_name);
  s.setValue("volume", volume);
  s.setValue("mic_gain", mic_gain);
  s.setValue("ptt_key", ptt_key);
  s.setValue("ptt_hold", ptt_hold);
  s.setValue("ui_scale_mode", ui_scale_mode);
  s.setValue("global_ptt_key", global_ptt_key);
  s.setValue("step_hz", step_hz);
  s.setValue("window_geometry", window_geometry);
  s.sync();
}

QString Settings::BaseUrl() const {
  return QString("%1://%2:%3").arg(tls ? "https" : "http").arg(host.trimmed()).arg(port);
}

QString Settings::WsUrl(const QString& path, const QString& token) const {
  return QString("%1://%2:%3%4?token=%5")
      .arg(tls ? "wss" : "ws")
      .arg(host.trimmed())
      .arg(port)
      .arg(path)
      .arg(token);
}

Settings::Target Settings::ParseTarget(const QString& typed, int port_field) {
  Target t;
  QString h = typed.trimmed();

  // The scheme, if the operator pasted one.
  if (h.startsWith("https://", Qt::CaseInsensitive)) {
    t.tls = true;
    h = h.mid(8);
  } else if (h.startsWith("http://", Qt::CaseInsensitive)) {
    h = h.mid(7);
  }

  // ⚠️ A PASTED URL CARRIES A PATH, AND A HOST WITH A SLASH IN IT RESOLVES TO
  // NOTHING. "https://radio.wa0o.com/" is what a browser's address bar hands
  // over, and it must mean the same as the bare name.
  const int slash = h.indexOf('/');
  if (slash >= 0) h = h.left(slash);

  // A port in the host string is the most specific thing said, so it wins.
  // ⚠️ NOT ON AN IPv6 LITERAL, where the colons are the address.
  bool port_in_host = false;
  const int colon = h.lastIndexOf(':');
  if (colon > 0 && !h.contains('[') && h.count(':') == 1) {
    bool ok = false;
    const int p = h.mid(colon + 1).toInt(&ok);
    if (ok && p > 0 && p <= 65535) {
      t.port = p;
      h = h.left(colon);
      port_in_host = true;
    }
  }

  t.host = h.trimmed();
  if (!port_in_host) {
    // ⚠️ 443 WHEN A SCHEME SAID https. See the note in the header: honouring a
    // stale 5002 here is a refused connection with nothing on screen to blame.
    t.port = t.tls ? 443 : (port_field > 0 ? port_field : 5002);
  }
  return t;
}

QString Settings::ProfileJson() const {
  QJsonObject o;
  o["mic_gain"] = mic_gain;
  o["volume"] = volume;
  o["ptt_key"] = ptt_key;
  o["ptt_hold"] = ptt_hold;
  o["global_ptt_key"] = global_ptt_key;
  o["step_hz"] = step_hz;
  o["ui_scale_mode"] = ui_scale_mode;
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void Settings::ApplyProfileJson(const QString& json) {
  const auto doc = QJsonDocument::fromJson(json.toUtf8());
  if (!doc.isObject()) return;
  const QJsonObject o = doc.object();

  // ⚠️ Each key only if PRESENT. A profile written by an older client is missing
  // keys a newer one knows about, and treating absent as zero would hand the
  // operator a mic gain of 0 - a dead transmitter - on their next login.
  if (o.contains("mic_gain")) mic_gain = o["mic_gain"].toInt(mic_gain);
  if (o.contains("volume")) volume = o["volume"].toInt(volume);
  if (o.contains("ptt_key")) ptt_key = o["ptt_key"].toInt(ptt_key);
  if (o.contains("ptt_hold")) ptt_hold = o["ptt_hold"].toBool(ptt_hold);
  if (o.contains("global_ptt_key")) global_ptt_key = o["global_ptt_key"].toString(global_ptt_key);
  if (o.contains("step_hz")) step_hz = o["step_hz"].toInt(step_hz);
  if (o.contains("ui_scale_mode")) ui_scale_mode = o["ui_scale_mode"].toString(ui_scale_mode);
}
