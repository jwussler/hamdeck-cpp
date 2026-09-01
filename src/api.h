#pragma once

#include "http.h"

#include <functional>
#include <mutex>
#include <string>

#include "audio.h"
#include "tx_audio.h"
#include "auth.h"
#include "config.h"
#include "recorder.h"
#include "session_stats.h"
#include "amp_tuner.h"
#include "tgxl.h"
#include "radio.h"

// Which listener accepted the request. This is the ONLY trustworthy source of
// "is the caller local" - see the comment on the listener split in main.cpp.
enum class Listener {
  kControl,    // 127.0.0.1 only. Trusted: no session required.
  kDashboard,  // LAN. Session required except for the always-anonymous routes.
};

// Host-side state that is NOT in the radio: the frequency entry buffer, the
// pre-mute levels to restore, and the software locks the reference host keeps in
// its config.
//
// ⚠️ It lives here, owned once, because InstallRoutes() runs TWICE - once per
// listener. Anything declared inside it would exist as two independent copies,
// so a buffer typed on the dashboard would be invisible to the control port and
// unmuting on one would restore a level the other never saw.
struct HostState {
  std::mutex mu;
  std::string freq_buffer;
  int pre_mute_af = 128;
  int pre_mute_sub_af = 128;
  bool vfo_locked = false;      // software lock, not the rig's LK
  bool diversity = false;
};

// ⚠️ ROUTES THAT KEY THE TRANSMITTER, so `can_transmit` can actually gate them.
//
// Exposed rather than buried in a lambda because the failure mode is a MISSING
// ENTRY, and a list nothing can test is a list that quietly goes stale. The C#
// reference gates exactly these; test_transmit_gate asserts the same set.
bool IsTransmitRoute(const std::string& path);

struct ApiDeps {
  RadioPoller* poller = nullptr;
  AuthService* auth = nullptr;
  RxAudioStream* rx_audio = nullptr;
  TxAudioReceiver* tx_audio = nullptr;
  HostState* host = nullptr;

  // ⚠️ HOW MUCH AUDIO IS STILL ON THE DEVICE, in milliseconds, or -1 if it
  // cannot be measured. Supplied as a callback so this file needs no ALSA
  // headers, and so a host with no real playback simply reports -1.
  //
  // This is what /api/ptt/off waits for. See the comment on that route.
  std::function<int()> queued_audio_ms;

  // Live config, and a way to persist it. Admin changes that are not written
  // back vanish on the next restart - which on a station host is the next power
  // cut, not some distant maintenance window.
  Config* config = nullptr;
  TgxlTuner* tgxl = nullptr;
  AmpTuner* amp = nullptr;
  Recorder* recorder = nullptr;
  SessionStats* stats = nullptr;
  std::function<bool(std::string&)> save_config;

  // ⚠️ WHERE PER-USER SETTINGS LIVE, one JSON file per user. Empty disables the
  // profile routes entirely rather than writing somewhere arbitrary.
  //
  // This is the operator's own preferences - mic gain above all, because a gain
  // that reverts to 100% pins the rig's ALC and puts a splattering signal on the
  // air. It is NOT a place for credentials, and never for a session token: it is
  // read back to any client that logs in as that user.
  std::string profile_dir;
  // True when the CAT backend is the simulator. Surfaced on /api/backend so a
  // test tool can PROVE it is not pointed at the station.
  bool simulated = false;
  // Off on the live station (verified 08/30/2026: /api/status returns 401 on the
  // LAN port). When set, the read-only routes are served without a session.
  bool allow_anonymous_status = false;
};

void InstallRoutes(HttpServer& server, Listener listener, int bound_port,
                   const ApiDeps& deps);
