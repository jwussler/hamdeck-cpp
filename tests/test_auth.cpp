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

#include <cassert>
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

  assert(AuthService::VerifyPassword("correct horse battery staple", kExternal));
  std::printf("interop:  external PBKDF2 hash verifies OK\n");

  assert(!AuthService::VerifyPassword("wrong password", kExternal));
  assert(!AuthService::VerifyPassword("", kExternal));
  std::printf("interop:  wrong password rejected\n");

  // Our own round trip. Weak evidence by itself - see the header comment.
  const std::string mine = AuthService::HashPassword("hunter2");
  assert(mine.rfind("pbkdf2:", 0) == 0);
  assert(AuthService::VerifyPassword("hunter2", mine));
  assert(!AuthService::VerifyPassword("hunter3", mine));

  // Two hashes of the same password must differ: the salt must be random. If
  // they matched, the salt would be constant and the whole point lost.
  assert(AuthService::HashPassword("hunter2") != mine);
  std::printf("salt:     random per hash\n");

  // A legacy bare-SHA256 hash must NOT be accepted - auth.h explains why that
  // upgrade path is deliberately absent rather than guessed at.
  assert(!AuthService::VerifyPassword("hunter2", "5e884898da28047151d0e56f8dc629"));
  assert(!AuthService::VerifyPassword("hunter2", ""));
  assert(!AuthService::VerifyPassword("hunter2", "pbkdf2:nothex:nothex"));
  std::printf("legacy:   non-PBKDF2 stored hashes rejected\n");

  // Sessions.
  AuthService auth(480);
  assert(!auth.IsConfigured());
  auth.AddUser("Joe", AuthService::HashPassword("s3cret"), /*is_admin=*/true);
  assert(auth.IsConfigured());

  assert(!auth.Login("joe", "wrong").has_value());
  const auto token = auth.Login("  JOE  ", "s3cret");   // case/space-insensitive, matches C#
  assert(token.has_value());
  assert(auth.ValidateSession(*token));
  assert(auth.IsAdmin(*token));
  assert(auth.Username(*token).value_or("") == "joe");
  assert(!auth.ValidateSession("not-a-real-token"));
  std::printf("session:  login, validate, admin flag, unknown token rejected\n");

  auth.Logout(*token);
  assert(!auth.ValidateSession(*token));
  std::printf("session:  logout invalidates\n");

  // Throttle: five failures locks the account.
  AuthService t(480);
  t.AddUser("bob", AuthService::HashPassword("pw"));
  for (int i = 0; i < AuthService::kMaxLoginFails; ++i) {
    assert(!t.Login("bob", "nope").has_value());
  }
  assert(t.IsLockedOut("bob"));
  std::printf("throttle: locked out after %d failures\n", AuthService::kMaxLoginFails);

  std::printf("PASS\n");
  return 0;
}
