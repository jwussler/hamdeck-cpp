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

  std::printf("PASS\n");
  return 0;
}
