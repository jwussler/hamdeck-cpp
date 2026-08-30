#pragma once

// Host configuration.
//
// Key names match the reference host's config exactly, so one file can describe
// either implementation and an operator does not have to learn a second schema.
//
// ⚠️ NO DEFAULT SHIPS AN ADDRESS OR A HOSTNAME. The reference host's config
// carries station LAN addresses as compiled-in defaults, and that source is
// public - CARRYOVER.md section 6 says a hostname in a public repo points every
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

  // API
  int  api_port = 5001;               // control, bound to loopback
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

  // Search order, first that exists.
  static std::vector<std::string> DefaultPaths();
};
