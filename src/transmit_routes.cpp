// Which routes put a signal on the air.
//
// ⚠️ ITS OWN TRANSLATION UNIT, WITH NO DEPENDENCIES, so the test that guards it links
// this file alone. Living inside api.cpp meant testing a route LIST required civetweb,
// ALSA and the whole server - and a list that is awkward to test is a list that goes
// stale, which is the only way this can fail.

#include <set>
#include <string>

#include "api.h"

// ⚠️ These are the routes that PUT A SIGNAL ON THE AIR without going near the
// audio websocket. The C++ host gated only /ws/tx, so `can_transmit = false`
// stopped an account feeding audio and did NOT stop it keying the rig, sending
// CW, or playing a voice memory - while the code beside /ws/tx said "a listener
// must not be able to key the rig just because they could log in". The reference
// host gates exactly this set (ApiServer.cs ~line 789); it was simply not ported.
bool IsTransmitRoute(const std::string& path) {
  static const std::set<std::string> exact = {
      "/api/ptt/on", "/api/ptt/off", "/api/ptt/key", "/api/ptt/unkey",
      "/api/ptt/toggle",
  };
  if (exact.count(path)) return true;
  for (const char* prefix : {"/api/cw/memory/", "/api/cw/send/", "/api/voice/play/"}) {
    // ⚠️ Prefix, and the trailing slash is load-bearing: without it "/api/cw/send"
    // would also match "/api/cw/sendable" if such a route were ever added.
    if (path.rfind(prefix, 0) == 0) return true;
  }
  return false;
}
