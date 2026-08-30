#pragma once

// Client settings.
//
// ⚠️ THREE RULES FROM CARRYOVER.md SECTION 6, ALL LEARNED THE HARD WAY:
//
// 1. Settings live OUTSIDE the install directory, so an update cannot overwrite
//    them. QSettings with the platform config location does this.
// 2. NO PASSWORD is stored. The session cookie is held in memory only; losing it
//    costs a login, and storing it costs a credential on disk.
// 3. Audio devices are stored by NAME, never by index. Indices shift when USB
//    devices come and go - that is what produced BadDeviceId and a dead
//    microphone.
//
// And a fourth, which is about this repo rather than the app: NEVER ship a
// default host. A hostname compiled into a public repo points every install at
// that station.

#include <QRect>
#include <QString>

class Settings {
 public:
  void Load();
  void Save() const;

  QString host;              // empty until the operator says. No default, ever.
  int port = 5002;           // the dashboard port; the control port is loopback-only
  QString username;
  QString rx_device_name;    // by NAME - see above
  QString tx_device_name;
  int volume = 80;

  // Geometry, clamped on restore - never trusted as written.
  QRect window_geometry;

  bool HasHost() const { return !host.trimmed().isEmpty(); }
  QString BaseUrl() const;
};
