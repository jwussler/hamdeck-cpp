#pragma once

// Host configuration.
//
// Key names match the reference host's config exactly, so one file can describe
// either implementation and an operator does not have to learn a second schema.
//
// ⚠️ NO DEFAULT SHIPS AN ADDRESS OR A HOSTNAME. The reference host's config
// carries station LAN addresses as compiled-in defaults, and that source is
// public - docs/internal/CARRYOVER.md section 6 says a hostname in a public repo points every
// install at that station, and an address does the same. Here every such field
// defaults to EMPTY, which means "not configured, feature off". An operator who
// wants the feature says where it lives.

#include <string>
#include <vector>

struct ConfigUser {
  std::string username;
  std::string password_hash;   // pbkdf2:<hex salt>:<hex hash>
  bool is_admin = false;
  bool can_transmit = true;
  // ⚠️ "This account is the operator sitting at the station."
  //
  // It exists because the amp tune's old test - did the request arrive on the
  // loopback listener - stopped meaning what it was written to mean. That test
  // was correct when the host ran ON the station PC, so loopback proved a human
  // was present. The rig now has its own box: loopback there proves the caller
  // is on the rig box, which is the one place nobody sits.
  //
  // So the question moved from WHERE a request came from to WHO sent it. This
  // right answers the new one, and it defaults to false: an account gets it by a
  // deliberate act, never by upgrading.
  bool is_station = false;
};

struct Config {
  // Radio
  std::string radio_port = "";        // empty = use the simulator
  int radio_baud = 38400;

  // Audio
  int record_sample_rate = 22050;     // RX wire format; the client expects this
  // ⚠️ EMPTY MEANS SYNTHETIC, not "the default device".
  // "default" is a real ALSA device on most systems - usually the desktop mixer.
  // Defaulting to it would mean a host started anywhere near a sound card
  // quietly streams that machine's audio to the operator, and on the transmit
  // side plays into whatever the desktop plays into. Real audio has to be asked
  // for by name, exactly like the CAT device.
  std::string alsa_capture_device = "";
  std::string alsa_playback_device = "";
  bool audio_stream_enabled = true;
  bool tx_audio_enabled = true;

  // Recording. ⚠️ Empty path means recording is OFF and its status route says
  // so - it does not silently pick a directory and start filling it.
  std::string record_path = "";
  int record_buffer_seconds = 60;    // the replay buffer
  int record_max_seconds = 10800;    // hard ceiling; 0 disables the ceiling
  int record_warning_seconds = 300;

  // PTT auto-record. ⚠️ OFF unless the operator turns it on: it records whoever
  // they are talking to, unasked, and that is their call to make once rather
  // than a default that arrives with an update.
  bool ptt_record_enabled = false;
  int ptt_record_seconds = 60;        // idle time after the last over
  int ptt_record_qsy_khz = 10;        // QSY from the start freq that ends it

  // API
  int  api_port = 5001;               // control, bound to loopback

  // ⚠️ TCP CAT PROXY, loopback only. 0 disables it. This is what lets N1MM (or
  // any logger) talk to the radio without a virtual serial-port splitter:
  // Configure Ports -> TCP -> 127.0.0.1:4532.
  //
  // ⚠️ It forwards CAT VERBATIM, including TX1;. Anything that can reach the
  // port can key the transmitter, which is why it binds loopback and why it is
  // OFF unless the operator asks for it. Turning it on is a decision about who
  // can key the rig, not a formatting preference.
  int  cat_proxy_port = 0;
  int  dashboard_port = 5002;         // LAN
  bool api_bind_lan = false;          // publishing the CONTROL port to the LAN
  bool allow_anonymous_status = false;

  // Safety
  int ptt_timeout_seconds = 180;      // 0 disables the transmit watchdog

  // Auth
  int web_session_timeout = 480;      // minutes
  bool admin_only_login = false;
  std::vector<ConfigUser> web_users;

  // Optional peripherals. Empty host = the feature is OFF and its routes report
  // themselves unavailable rather than pretending.
  std::string tgxl_host = "";
  int tgxl_port = 9010;
  std::string kmtronic_host = "";
  int kmtronic_port = 12345;

  // Loads from `path`. A missing file is NOT an error - defaults are usable.
  // A malformed file IS an error: silently falling back to defaults would run
  // the station on settings the operator did not choose and thinks they changed.
  static bool Load(const std::string& path, Config& out, std::string& error);

  // ⚠️ Writes the file back, PRESERVING KEYS IT DOES NOT KNOW.
  //
  // A writer that serialises its own struct silently deletes everything else in
  // the file - a setting a newer build added, a comment key, anything the
  // operator put there by hand. It reads the existing document, updates only the
  // fields it manages, and writes that back.
  //
  // Writes via a temporary file and renames, so an interrupted write cannot
  // leave a half-written config that then fails to parse on the next start.
  bool Save(const std::string& path, std::string& error) const;

  // Search order, first that exists.
  static std::vector<std::string> DefaultPaths();
};
