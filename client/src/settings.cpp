#include "settings.h"

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
  username = s.value("username", "").toString();
  rx_device_name = s.value("rx_device_name", "").toString();
  tx_device_name = s.value("tx_device_name", "").toString();
  volume = s.value("volume", 80).toInt();
  window_geometry = s.value("window_geometry", QRect()).toRect();

  // A password key must never exist. If an older build wrote one, remove it
  // rather than leave a credential lying in a config file.
  if (s.contains("password")) s.remove("password");
}

void Settings::Save() const {
  QSettings s(QSettings::IniFormat, QSettings::UserScope, kOrg, kApp);
  s.setValue("host", host);
  s.setValue("port", port);
  s.setValue("username", username);
  s.setValue("rx_device_name", rx_device_name);
  s.setValue("tx_device_name", tx_device_name);
  s.setValue("volume", volume);
  s.setValue("window_geometry", window_geometry);
  s.sync();
}

QString Settings::BaseUrl() const {
  return QString("http://%1:%2").arg(host.trimmed()).arg(port);
}
