#pragma once

// Runtime logging, off by default.
//
// ⚠️ THE POINT OF THIS IS TO CATCH THINGS THAT ARE WRONG BUT NOT FATAL. The host
// already refuses loudly when something breaks - a named CAT device that will
// not open exits 1, a malformed config is fatal, a failed exchange reports
// disconnected rather than a stale reading. What it could not do until now is
// show the things it recovers from: a reply that arrived for the wrong command,
// an exchange that timed out and was retried on the next cycle, a route
// answering 401 to a client that thinks it is logged in. Each is invisible in a
// working system and each is a bug in waiting.
//
// Levels, chosen so that turning it up is safe on the live station:
//   0 quiet    startup banner only - the default, unchanged behaviour
//   1 info     state changes: PTT, watchdog, sessions, audio clients
//   2 verbose  + every HTTP request with its outcome, + a stats heartbeat
//   3 trace    + every CAT exchange, its reply and its timing
//
// ⚠️ Level 3 is roughly 30-60 lines a second against a real radio, because the
// poller runs every 200 ms and asks for several things each cycle. journald's
// default rate limit is 10000 messages per 30 s per unit, so trace fits, but
// only just - and it is not something to leave on for a weekend.
//
// ⚠️ NOTHING HERE CHANGES BEHAVIOUR. No timing, no retries, no early returns.
// A log level that alters what the radio does would make every observation
// suspect - the observer effect is not a joke on a 200 ms serial poll.

#include <cstdint>
#include <string>
#include <string_view>

namespace hdlog {

enum Level : int { kQuiet = 0, kInfo = 1, kVerbose = 2, kTrace = 3 };

// From --verbose / --trace / HAMDECK_LOG_LEVEL. Called once at startup.
void SetLevel(int level);
int level();

inline bool On(int want) { return level() >= want; }

// One line, timestamped and tagged. Serialised, because two threads writing a
// line each is how you get half of one inside the other.
void Line(int want, std::string_view tag, std::string_view msg);

// Bytes as they went over the wire, with the control characters made visible.
// A CAT command is mostly printable and ends in ';', but a framing bug looks
// exactly like nothing at all unless the bytes are shown.
std::string Escape(std::string_view raw);

}  // namespace hdlog
