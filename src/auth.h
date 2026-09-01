#pragma once

// Session auth, matching the C# AuthService byte for byte where it has to.
//
// ⚠️ THE HASH FORMAT IS A COMPATIBILITY CONTRACT, not an implementation detail.
// Existing password hashes must keep verifying, so these constants are fixed:
//
//     pbkdf2:<16-byte salt as lowercase hex>:<32-byte hash as lowercase hex>
//     PBKDF2-HMAC-SHA256, 350000 iterations
//
// Change any of them and every stored credential silently stops working.
//
// The C# host also transparently upgrades legacy bare-SHA256 hashes to PBKDF2 on
// successful login. That path is NOT implemented here: it needs a live look at
// whether any such hash still exists on the station, and inventing a second
// accepted hash format on a guess is how you widen an auth surface by accident.

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>

struct SessionInfo {
  std::string username;
  bool is_admin = false;
  bool can_transmit = true;
  std::chrono::steady_clock::time_point created;
  std::chrono::steady_clock::time_point last_activity;
};

struct UserInfo {
  std::string password_hash;
  bool is_admin = false;
  bool can_transmit = true;
};

class AuthService {
 public:
  explicit AuthService(int session_timeout_minutes = 480)
      : session_timeout_minutes_(session_timeout_minutes) {}

  static std::string HashPassword(const std::string& password);
  static bool VerifyPassword(const std::string& password, const std::string& stored);

  void AddUser(const std::string& username, const std::string& password_hash,
               bool is_admin = false, bool can_transmit = true);

  bool IsConfigured() const;

  // Returns a session token, or nullopt. Callers MUST apply the failure delay
  // themselves - see kFailureDelay.
  std::optional<std::string> Login(const std::string& username, const std::string& password);

  bool ValidateSession(const std::string& token);   // sliding: refreshes last_activity
  bool IsAdmin(const std::string& token) const;
  bool CanTransmit(const std::string& token) const;
  std::optional<std::string> Username(const std::string& token) const;
  void Logout(const std::string& token);
  bool IsLockedOut(const std::string& username) const;

  // ── Administration ────────────────────────────────────────────────────────
  bool RemoveUser(const std::string& username);
  bool ChangePassword(const std::string& username, const std::string& new_hash);
  bool SetCanTransmit(const std::string& username, bool allow);
  int  KillUserSessions(const std::string& username);

  struct UserRow { std::string username; bool is_admin; bool can_transmit; };
  std::vector<UserRow> ListUsers() const;

  // ⚠️ How many OTHER sessions have touched the host within `within_seconds`.
  //
  // THE EXCLUSION IS THE WHOLE POINT. Every authenticated request refreshes its
  // own session's last_activity, so a helper that polls "is anyone else
  // operating the station?" refreshes itself a millisecond before it asks, and
  // without the exclusion the answer is permanently, confidently yes. It would
  // then stand down forever in favour of a client that is not there.
  int ActiveSessionsExcluding(const std::string& exclude_token,
                              int within_seconds) const;

  struct SessionRow {
    std::string token_short, username;
    bool is_admin, can_transmit;
    long long idle_seconds;
  };
  std::vector<SessionRow> ListSessions() const;

  // ⚠️ HOW MANY ADMINS ARE LEFT. Used to refuse the removal - or the
  // demotion - of the last one. A host nobody can administer is a host that
  // needs a config file edited by hand and a restart, on a box that may be at
  // the other end of a radio link.
  int AdminCount() const;

  // The stored hash, so the config mirror can write users back without ever
  // handling a plaintext password.
  std::string PasswordHashOf(const std::string& username) const;

  int session_timeout_minutes() const { return session_timeout_minutes_; }

  // Throttle: five failures locks the account for five minutes. Matches C#.
  static constexpr int kMaxLoginFails = 5;
  static constexpr int kLockoutMinutes = 5;

  // Every failed or rejected login sleeps before answering, so a caller cannot
  // tell "no such user" from "wrong password" by timing, and cannot spin through
  // guesses at network speed.
  static constexpr int kFailureDelayMs = 500;

 private:
  void PurgeExpired();

  mutable std::mutex mu_;
  std::map<std::string, UserInfo> users_;
  std::map<std::string, SessionInfo> sessions_;
  std::map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> throttle_;
  int session_timeout_minutes_;
};
