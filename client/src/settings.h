#pragma once

// Client settings.
//
// ⚠️ THREE RULES FROM docs/internal/CARRYOVER.md SECTION 6, ALL LEARNED THE HARD WAY:
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
#include <Qt>
#include <QString>

class Settings {
 public:
  void Load();
  void Save() const;

  // ── The portable profile ───────────────────────────────────────────────────
  // ⚠️ WHAT FOLLOWS THE OPERATOR, AND WHAT DOES NOT.
  //
  // Portable: the preferences that are about the PERSON - mic gain above all,
  // because a gain that reverts pins the rig's ALC and puts a splattering signal
  // on the air.
  //
  // Deliberately NOT portable, because they describe a MACHINE and carrying them
  // would actively break a second PC:
  //   host, port, username  - which station and who; set at the connect screen
  //   rx/tx_device_name     - a sound device that exists on one computer only.
  //                           Carrying this is how an operator ends up armed
  //                           against a microphone that is not there.
  //   window_geometry       - a position on a screen that may not exist
  //
  // ⚠️ NEVER a password or a token. The profile is handed back to anything that
  // can log in as this user.
  QString ProfileJson() const;
  void ApplyProfileJson(const QString& json);

  QString host;              // empty until the operator says. No default, ever.
  int port = 5002;           // the dashboard port; the control port is loopback-only
  QString username;
  QString rx_device_name;    // by NAME - see above
  QString tx_device_name;
  int volume = 80;

  // ⚠️ MIC GAIN MUST PERSIST. It was applied to the audio path and never saved,
  // so every restart silently reverted it to 100% - and 100% is what pins the
  // rig's ALC and puts a splattering signal on the air. An operator who has set
  // their gain correctly must not have to set it again, and must certainly not
  // be returned to the wrong value without being told.
  int mic_gain = 100;   // percent; 100 = unity

  // PTT hotkey, as a Qt::Key value plus a mode.
  //
  // ⚠️ Defaults to Pause/Break, NOT F13. F13 is the better key in theory - no
  // keyboard sends it, so nothing can conflict - but that is also why nobody can
  // press it without a footswitch or a programmable key mapped to it. A default
  // the operator cannot press is not a default.
  int ptt_key = Qt::Key_Pause;   // symbol, not a magic number
  bool ptt_hold = true;          // hold-to-talk; false = press-to-toggle

  // UI density: "Auto" or a fixed percentage from Backend's mode table. Stored
  // as the LABEL rather than an index, so inserting a mode later cannot silently
  // move every existing operator to a different scale.
  QString ui_scale_mode = "Auto";

  // The SYSTEM-WIDE PTT key, by label ("Off", "Ctrl+Alt+Space", ...). Stored as
  // the label rather than an index so inserting a choice later cannot silently
  // move an operator onto a different key - which, for a transmit control, is
  // not a cosmetic problem.
  QString global_ptt_key = "Off";

  // Tuning step for the wheel and the ± keys, in Hz. 1 kHz is the reference
  // panel's default and the one that suits SSB.
  int step_hz = 1000;

  // Geometry, clamped on restore - never trusted as written.
  QRect window_geometry;

  // ⚠️ TLS IS A PROPERTY OF THE TARGET, NOT A PREFERENCE. The station is reached
  // three ways - a bare address on the LAN, the same address over WireGuard, and
  // https://radio.wa0o.com through the reverse proxy - and only the third is
  // encrypted. It is stored per install because it describes WHICH host, exactly
  // like `host` and `port`, and for the same reason it is NOT in ProfileJson.
  bool tls = false;

  bool HasHost() const { return !host.trimmed().isEmpty(); }

  // ── Building the two URL families ──────────────────────────────────────────
  // ⚠️ THE WEBSOCKETS MUST FOLLOW THE SCHEME. `ws://` to a TLS origin does not
  // fail politely: Caddy answers the upgrade with a 400 and the panel connects,
  // logs in, shows live status over REST, and is silently deaf - no RX audio and
  // no transmit - because those are the only two things that ride the socket.
  // Both were hand-built at their call sites until 09/03/2026; they are here now
  // so a scheme can only be got wrong in one place.
  QString BaseUrl() const;
  QString WsUrl(const QString& path, const QString& token) const;

  // What the operator typed, resolved into a target.
  //
  // ⚠️ A SCHEME IN THE HOST FIELD WINS OVER THE PORT FIELD. Typing
  // `https://radio.wa0o.com` and leaving the port at 5002 must not produce
  // https on 5002 - that is a connection refused, on a screen that shows the
  // operator both a hostname and a port and no reason to suspect either.
  // A port typed INTO the host string still wins over both, because it is the
  // most specific thing the operator said.
  struct Target {
    QString host;
    int port = 5002;
    bool tls = false;
  };
  static Target ParseTarget(const QString& typed, int port_field);
};
