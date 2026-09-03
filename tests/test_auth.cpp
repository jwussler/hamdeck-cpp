// Auth compatibility and behaviour.
//
// The first test is the one that matters on cutover day. The stored hash below
// was produced by an INDEPENDENT PBKDF2 implementation (Python hashlib), not by
// this code, with the same parameters the C# host uses: SHA256, 350000
// iterations, 16-byte salt, 32-byte output, lowercase hex. If this test passes,
// hashes written by the C# host verify here. A round-trip test of our own
// HashPassword against our own VerifyPassword would pass even if every parameter
// were wrong, so it proves nothing about compatibility - it is included second,
// for what it is worth, not first.

#include "check.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <string>

#include "../src/auth.h"

int main() {
  // Generated externally:
  //   hashlib.pbkdf2_hmac('sha256', b'correct horse battery staple',
  //                       bytes.fromhex('000102...0f'), 350000, 32)
  const std::string kExternal =
      "pbkdf2:000102030405060708090a0b0c0d0e0f:"
      "83c64222debd9cbd547203bb148e61974d8685f6d6b6c6f2c9c26ac62e7738d2";

  CHECK(AuthService::VerifyPassword("correct horse battery staple", kExternal));
  std::printf("interop:  external PBKDF2 hash verifies OK\n");

  CHECK(!AuthService::VerifyPassword("wrong password", kExternal));
  CHECK(!AuthService::VerifyPassword("", kExternal));
  std::printf("interop:  wrong password rejected\n");

  // Our own round trip. Weak evidence by itself - see the header comment.
  const std::string mine = AuthService::HashPassword("hunter2");
  CHECK(mine.rfind("pbkdf2:", 0) == 0);
  CHECK(AuthService::VerifyPassword("hunter2", mine));
  CHECK(!AuthService::VerifyPassword("hunter3", mine));

  // Two hashes of the same password must differ: the salt must be random. If
  // they matched, the salt would be constant and the whole point lost.
  CHECK(AuthService::HashPassword("hunter2") != mine);
  std::printf("salt:     random per hash\n");

  // A legacy bare-SHA256 hash must NOT be accepted - auth.h explains why that
  // upgrade path is deliberately absent rather than guessed at.
  CHECK(!AuthService::VerifyPassword("hunter2", "5e884898da28047151d0e56f8dc629"));
  CHECK(!AuthService::VerifyPassword("hunter2", ""));
  CHECK(!AuthService::VerifyPassword("hunter2", "pbkdf2:nothex:nothex"));
  std::printf("legacy:   non-PBKDF2 stored hashes rejected\n");

  // Sessions.
  AuthService auth(480);
  CHECK(!auth.IsConfigured());
  auth.AddUser("Joe", AuthService::HashPassword("s3cret"), /*is_admin=*/true,
               /*can_transmit=*/true, /*is_station=*/false);
  CHECK(auth.IsConfigured());

  CHECK(!auth.Login("joe", "wrong").has_value());
  const auto token = auth.Login("  JOE  ", "s3cret");   // case/space-insensitive, matches C#
  CHECK(token.has_value());
  CHECK(auth.ValidateSession(*token));
  CHECK(auth.IsAdmin(*token));
  CHECK(auth.Username(*token).value_or("") == "joe");
  CHECK(!auth.ValidateSession("not-a-real-token"));
  std::printf("session:  login, validate, admin flag, unknown token rejected\n");

  auth.Logout(*token);
  CHECK(!auth.ValidateSession(*token));
  std::printf("session:  logout invalidates\n");

  // Throttle: five failures locks the account.
  AuthService t(480);
  t.AddUser("bob", AuthService::HashPassword("pw"), false, true, false);
  for (int i = 0; i < AuthService::kMaxLoginFails; ++i) {
    CHECK(!t.Login("bob", "nope").has_value());
  }
  CHECK(t.IsLockedOut("bob"));
  std::printf("throttle: locked out after %d failures\n", AuthService::kMaxLoginFails);

  // ── ⚠️ A LOGIN MUST NOT STALL THE HOST ───────────────────────────────────
  // Login held mu_ across 350,000 PBKDF2 rounds - ~200 ms - and ValidateSession
  // takes the same mutex on EVERY request. One login froze the whole host, and a
  // phone retrying on a flaky link does that repeatedly. On a box that keys a
  // transmitter it means /api/ptt/off queues behind somebody's password.
  {
    AuthService c;
    c.AddUser("op", AuthService::HashPassword("pw"), false, true, false);
    const auto tok = c.Login("op", "pw");
    CHECK(tok.has_value());

    std::atomic<bool> go{false};
    std::thread slow([&] {
      while (!go.load()) std::this_thread::yield();
      for (int i = 0; i < 4; ++i) c.Login("op", "wrong");   // ~800 ms of PBKDF2
    });
    go.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));  // let it get in

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) CHECK(c.ValidateSession(*tok));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    slow.join();
    if (ms > 150) std::fprintf(stderr, "ValidateSession blocked for %lld ms\n", (long long)ms);
    // Generous on purpose: the point is 50 lookups take milliseconds, not the
    // ~800 ms of key derivation running beside them.
    CHECK(ms <= 150);
    std::printf("lock:     50 session checks took %lld ms beside a running login\n",
                (long long)ms);
  }

  // ── ⚠️ AN EXPIRED SESSION KEEPS NO RIGHTS ────────────────────────────────
  // IsAdmin/CanTransmit/IsStation answered from the session map alone, so an
  // expired session kept every right. On the loopback listener nothing ever
  // calls ValidateSession, so an expired ADMIN token opened /api/admin/*
  // indefinitely - and PurgeExpired only ran on a successful login.
  {
    AuthService z(0);   // zero-minute timeout: every session is already expired
    z.AddUser("ghost", AuthService::HashPassword("pw"), true, true, true);
    const auto tok = z.Login("ghost", "pw");
    CHECK(tok.has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(!z.ValidateSession(*tok));
    CHECK(!z.IsAdmin(*tok));
    CHECK(!z.CanTransmit(*tok));
    CHECK(!z.IsStation(*tok));
    CHECK(!z.Username(*tok).has_value());
    std::printf("expiry:   an expired session is not admin, cannot transmit, has no name\n");
  }

  std::printf("PASS\n");
  return 0;
}
