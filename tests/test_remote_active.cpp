// "Is somebody else operating the station right now?"
//
// This is the gate for the Wavelog push handing off between the desktop pusher
// and the remote client. Both failure modes are silent, so both are asserted
// here rather than reasoned about:
//
//   - answer YES when nobody is there  -> the desktop pusher stands down
//     forever and Wavelog quietly keeps showing an old frequency
//   - answer NO when somebody is there -> two pushers race on the same Wavelog
//     radio row and the log shows whichever POST happened to land last
//
// ⚠️ The defect this was written for: an authenticated request refreshes its
// OWN session on the way in, so a caller that does not exclude itself always
// finds a live session and always answers yes. Reintroduce that (drop the
// exclude_token test in ActiveSessionsExcluding) and "sees only itself" below
// fails.

#include "check.h"
#include <chrono>
#include <cstdio>
#include <thread>

#include "../src/auth.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  AuthService a(480);
  a.AddUser("pusher", AuthService::HashPassword("pw1"), false, true);
  a.AddUser("op",     AuthService::HashPassword("pw2"), false, true);

  // ── The desktop pusher logs in, and is the only thing on the host ────────
  const auto pusher = a.Login("pusher", "pw1");
  CHECK(pusher.has_value());

  // ⚠️ THE ONE THAT MATTERS. The pusher polls; that refreshed its own session a
  // moment ago. Excluding itself, it must see NOBODY.
  CHECK(a.ActiveSessionsExcluding(*pusher, 15) == 0);
  std::printf("alone:    pusher sees only itself -> 0 others\n");

  // Without the exclusion the very same state answers 1 - which is exactly the
  // bug. Asserting it here means the two answers can never quietly converge.
  CHECK(a.ActiveSessionsExcluding("", 15) == 1);
  std::printf("no-excl:  same instant, excluding nothing -> 1 (the bug's answer)\n");

  // ── The operator opens the remote client ────────────────────────────────
  const auto op = a.Login("op", "pw2");
  CHECK(op.has_value());
  CHECK(a.ActiveSessionsExcluding(*pusher, 15) == 1);
  // and symmetrically, so neither side is special-cased
  CHECK(a.ActiveSessionsExcluding(*op, 15) == 1);
  std::printf("both:     each sees exactly one other\n");

  // ── The operator closes the client: the session still EXISTS ────────────
  // A session lives for hours. Being logged in is not sitting at the radio, and
  // the window is the only thing that tells them apart.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  CHECK(a.ActiveSessionsExcluding(*pusher, 0) == 0);
  std::printf("stale:    a session idle past the window stops counting\n");

  // ...and touching it brings it straight back, so the handoff is reversible.
  CHECK(a.ValidateSession(*op));
  CHECK(a.ActiveSessionsExcluding(*pusher, 0) == 1);
  std::printf("resumed:  one request re-arms it\n");

  // ── Logout removes it outright, not merely ages it out ──────────────────
  a.Logout(*op);
  CHECK(a.ActiveSessionsExcluding(*pusher, 15) == 0);
  std::printf("logout:   session gone -> pusher takes the station back\n");

  std::printf("\nremote-active: all checks passed\n");
  return 0;
}
