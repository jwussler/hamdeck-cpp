// Which routes put a signal on the air.
//
// ⚠️ THE FAILURE MODE HERE IS A MISSING ENTRY, and a missing entry is invisible: the
// route works, the button works, and an account that was supposed to be receive-only
// keys the transmitter. The C++ host gated only /ws/tx for its whole life, so
// can_transmit=false blocked feeding AUDIO and did not block keying the rig - while the
// comment beside /ws/tx claimed "a listener must not be able to key the rig just because
// they could log in".
//
// The set below is the reference host's, from ApiServer.cs ~line 789. Both lists must
// stay identical, so the assertions name each route rather than counting them.

#include "check.h"
#include <cstdio>
#include <string>

#include "../src/api.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  // ── Everything the reference host gates ──────────────────────────────────
  const char* transmits[] = {
      "/api/ptt/on", "/api/ptt/key", "/api/ptt/toggle",
      // ⚠️ THE TUNERS KEY THE TRANSMITTER and were never in the set. main.cpp: the
      // TGXL tune saves power and mode, sets 15 W CW, KEYS THE RIG, tunes, then puts
      // both back; /api/tune is the rig's internal ATU and keys it too. Any session,
      // including can_transmit=false, could put a carrier on the air through them.
      "/api/tune", "/api/tune/tgxl", "/api/tgxl/tune",
      "/api/cw/memory/1", "/api/cw/memory/5",
      "/api/cw/send/CQ%20CQ",
      "/api/voice/play/1", "/api/voice/play/4",
  };
  for (const char* p : transmits) {
    if (!IsTransmitRoute(p)) {
      std::fprintf(stderr, "NOT GATED: %s\n", p);
    }
    CHECK(IsTransmitRoute(p));
  }
  std::printf("gated:    all %zu transmitting routes\n",
              sizeof(transmits) / sizeof(transmits[0]));

  // ── And nothing else, or a receive-only operator loses their whole panel ──
  // ⚠️ Over-gating is a real failure too, just a louder one: a listener who cannot
  // change AGC or read a meter will report the app as broken.
  const char* harmless[] = {
      "/api/status", "/api/status/full", "/api/health", "/api/meters",
      "/api/freq", "/api/freq/get", "/api/freq/set/14074000",
      "/api/mode/usb", "/api/agc/fast", "/api/volume/up", "/api/ant/1",
      "/api/cw-speed/get", "/api/cw-speed/set/25", "/api/cw/status", "/api/cw/stop",
      "/api/voice/status", "/api/voice/stop",
      "/api/remote/status", "/api/tx-audio/status", "/api/record/start",
      // Reading whether a tuner is running transmits nothing.
      "/api/tune/tgxl/status", "/api/tune/amp/status",
  };
  for (const char* p : harmless) {
    if (IsTransmitRoute(p)) {
      std::fprintf(stderr, "OVER-GATED: %s\n", p);
    }
    CHECK(!IsTransmitRoute(p));
  }
  std::printf("clear:    %zu non-transmitting routes left alone\n",
              sizeof(harmless) / sizeof(harmless[0]));

  // ⚠️ The neighbours that matter. cw-speed and cw/stop sit right beside cw/send, and
  // voice/status right beside voice/play - a prefix written one character shorter
  // swallows them and a receive-only operator loses controls that transmit nothing.
  CHECK(!IsTransmitRoute("/api/cw-speed/set/30"));
  CHECK(!IsTransmitRoute("/api/cw/stop"));
  CHECK(!IsTransmitRoute("/api/voice/stop"));
  CHECK(IsTransmitRoute("/api/cw/send/E"));
  std::printf("prefixes: cw/send and voice/play match, their neighbours do not\n");

  // ── ⚠️ NEVER GATE THE WAY OUT ─────────────────────────────────────────────
  // can_transmit is pushed into LIVE sessions (auth.cpp SetCanTransmit), so gating
  // the unkey routes means revoking transmit from someone mid-over answers their own
  // stop button with 403 and the rig stays keyed until the 180 s watchdog. A
  // permission check must never block the direction that makes the station safe.
  CHECK(!IsTransmitRoute("/api/ptt/off"));
  CHECK(!IsTransmitRoute("/api/ptt/unkey"));
  std::printf("way out:  unkeying is never refused\n");

  // A bare prefix with no argument is not a route this host serves, and must not be
  // treated as one by accident.
  CHECK(!IsTransmitRoute("/api/cw/send"));
  CHECK(!IsTransmitRoute("/api/voice/play"));
  std::printf("bare:     the prefixes need an argument to match\n");

  std::printf("\ntransmit-gate: all checks passed\n");
  return 0;
}
