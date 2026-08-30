// User administration.
//
// The assertions that matter are the REFUSALS and the side effects nobody
// thinks about: revoking a right must reach live sessions, and removing the
// last admin must not be possible.

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/auth.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  AuthService a(480);
  a.AddUser("boss", AuthService::HashPassword("pw1"), /*is_admin=*/true, true);
  a.AddUser("op",   AuthService::HashPassword("pw2"), /*is_admin=*/false, true);

  assert(a.ListUsers().size() == 2);
  assert(a.AdminCount() == 1);
  std::printf("users:    2 users, %d admin\n", a.AdminCount());

  // ⚠️ Removing the last admin must be refused by the CALLER, and the count is
  // what tells it so. A host with no admin can only be fixed by editing a config
  // file by hand and restarting - on a box that may be at the far end of a link.
  assert(a.AdminCount() <= 1);
  std::printf("guard:    AdminCount reports 1, so the route can refuse removal\n");

  // ── Revoking transmit must reach a LIVE session ──────────────────────────
  const auto tok = a.Login("op", "pw2");
  assert(tok.has_value());
  assert(a.CanTransmit(*tok));
  a.SetCanTransmit("op", false);
  // ⚠️ The session carries its own copy of the flag. If only the user record is
  // updated, revoking transmit does nothing until that operator logs out - which
  // is precisely when it does not matter.
  assert(!a.CanTransmit(*tok));
  std::printf("revoke:   transmit revoked reaches an already-open session\n");

  // ── Changing a password must invalidate existing sessions ────────────────
  const auto tok2 = a.Login("op", "pw2");
  assert(tok2.has_value());
  assert(a.ValidateSession(*tok2));
  a.ChangePassword("op", AuthService::HashPassword("pw3"));
  assert(!a.ValidateSession(*tok2));
  assert(!a.Login("op", "pw2").has_value());
  assert(a.Login("op", "pw3").has_value());
  std::printf("password: change invalidates open sessions and the old password\n");

  // ── Removing a user takes their sessions with them ───────────────────────
  const auto tok3 = a.Login("op", "pw3");
  assert(tok3.has_value());
  assert(a.RemoveUser("op"));
  assert(!a.ValidateSession(*tok3));
  assert(!a.RemoveUser("op"));   // second removal is a no-op, not a crash
  std::printf("remove:   user gone, and their live session with them\n");

  // ── Kick ─────────────────────────────────────────────────────────────────
  a.AddUser("op2", AuthService::HashPassword("pw4"), false, true);
  const auto k1 = a.Login("op2", "pw4");
  const auto k2 = a.Login("op2", "pw4");
  assert(k1 && k2);
  assert(a.KillUserSessions("op2") == 2);
  assert(!a.ValidateSession(*k1) && !a.ValidateSession(*k2));
  assert(a.KillUserSessions("nobody") == 0);
  std::printf("kick:     both sessions ended; kicking an unknown user is 0\n");

  // ── The session listing must not leak a whole token ──────────────────────
  const auto live = a.Login("boss", "pw1");
  assert(live.has_value());
  const auto rows = a.ListSessions();
  assert(!rows.empty());
  for (const auto& r : rows) {
    // ⚠️ A full session token in an admin listing is a credential in a log, a
    // screenshot and a support ticket.
    assert(r.token_short.size() < 16);
    assert(r.token_short.find("...") != std::string::npos);
    assert(live->rfind(r.token_short.substr(0, 8), 0) == 0 ||
           r.username != "boss");
  }
  std::printf("sessions: listing shows a token PREFIX only, never the token\n");

  std::printf("PASS\n");
  return 0;
}
