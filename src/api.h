#pragma once

#include "http.h"

#include <string>

#include "audio.h"
#include "auth.h"
#include "radio.h"

// Which listener accepted the request. This is the ONLY trustworthy source of
// "is the caller local" - see the comment on the listener split in main.cpp.
enum class Listener {
  kControl,    // 127.0.0.1 only. Trusted: no session required.
  kDashboard,  // LAN. Session required except for the always-anonymous routes.
};

struct ApiDeps {
  RadioPoller* poller = nullptr;
  AuthService* auth = nullptr;
  RxAudioStream* rx_audio = nullptr;
  // Off on the live station (verified 08/30/2026: /api/status returns 401 on the
  // LAN port). When set, the read-only routes are served without a session.
  bool allow_anonymous_status = false;
};

void InstallRoutes(HttpServer& server, Listener listener, int bound_port,
                   const ApiDeps& deps);
