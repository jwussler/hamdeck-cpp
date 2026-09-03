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
      "/api/ptt/on", "/api/ptt/key", "/api/ptt/toggle",

      // ⚠️ THE TUNERS KEY THE RIG, and they were missing for the whole life of this
      // list. /api/tune/tgxl saves the power and mode, sets 15 W CW, KEYS THE
      // TRANSMITTER, tunes, then puts both back (main.cpp, TgxlTuner); /api/tune is
      // the rig's internal ATU (AC002;) and keys it too. Neither is /ws/tx and
      // neither was in this set, so ANY session - including one explicitly denied
      // transmit - could put a carrier on the air by asking a tuner to work.
      // /api/tune/amp is gated separately and more strictly (is_station).
      "/api/tune", "/api/tune/tgxl", "/api/tgxl/tune",

      // ⚠️ NOT /api/ptt/off AND NOT /api/ptt/unkey. They were here, and gating them
      // was backwards: can_transmit is pushed into LIVE sessions
      // (AuthService::SetCanTransmit), so revoking transmit from an operator who is
      // mid-over answered their own stop button with 403 and left the rig keyed
      // until the 180 s watchdog. A permission check must never refuse the direction
      // that makes the station safe. /api/ptt/toggle stays: it can KEY.
  };
  if (exact.count(path)) return true;
  for (const char* prefix : {"/api/cw/memory/", "/api/cw/send/", "/api/voice/play/"}) {
    // ⚠️ Prefix, and the trailing slash is load-bearing: without it "/api/cw/send"
    // would also match "/api/cw/sendable" if such a route were ever added.
    if (path.rfind(prefix, 0) == 0) return true;
  }
  return false;
}
