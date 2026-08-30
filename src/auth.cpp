#include "auth.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace {

constexpr int  kSaltBytes  = 16;
constexpr int  kHashBytes  = 32;
constexpr int  kIterations = 350000;
constexpr char kPrefix[]   = "pbkdf2:";

std::string ToHexLower(const unsigned char* p, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    out.push_back(kHex[p[i] >> 4]);
    out.push_back(kHex[p[i] & 0x0f]);
  }
  return out;
}

std::optional<std::string> FromHex(const std::string& hex) {
  if (hex.size() % 2 != 0) return std::nullopt;
  std::string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    if (!std::isxdigit(static_cast<unsigned char>(hex[i])) ||
        !std::isxdigit(static_cast<unsigned char>(hex[i + 1]))) {
      return std::nullopt;
    }
    out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

std::string Derive(const std::string& password, const std::string& salt) {
  unsigned char out[kHashBytes];
  PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                    reinterpret_cast<const unsigned char*>(salt.data()),
                    static_cast<int>(salt.size()), kIterations, EVP_sha256(),
                    kHashBytes, out);
  return ToHexLower(out, kHashBytes);
}

std::string LowerTrim(const std::string& s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  auto e = s.find_last_not_of(" \t\r\n");
  std::string t = s.substr(b, e - b + 1);
  std::transform(t.begin(), t.end(), t.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return t;
}

}  // namespace

std::string AuthService::HashPassword(const std::string& password) {
  unsigned char salt[kSaltBytes];
  RAND_bytes(salt, kSaltBytes);
  const std::string salt_str(reinterpret_cast<char*>(salt), kSaltBytes);
  return std::string(kPrefix) + ToHexLower(salt, kSaltBytes) + ":" +
         Derive(password, salt_str);
}

bool AuthService::VerifyPassword(const std::string& password, const std::string& stored) {
  if (stored.rfind(kPrefix, 0) != 0) return false;   // legacy SHA256 not accepted - see auth.h
  const std::string body = stored.substr(sizeof(kPrefix) - 1);
  const auto colon = body.find(':');
  if (colon == std::string::npos) return false;

  const auto salt = FromHex(body.substr(0, colon));
  if (!salt) return false;
  const std::string expect = body.substr(colon + 1);
  const std::string actual = Derive(password, *salt);

  // Constant-time. A byte-by-byte compare leaks how much of the hash matched,
  // which is enough to reconstruct it one byte at a time given enough tries.
  if (actual.size() != expect.size()) return false;
  return CRYPTO_memcmp(actual.data(), expect.data(), actual.size()) == 0;
}

void AuthService::AddUser(const std::string& username, const std::string& password_hash,
                          bool is_admin, bool can_transmit) {
  std::lock_guard<std::mutex> lock(mu_);
  users_[LowerTrim(username)] = UserInfo{password_hash, is_admin, can_transmit};
}

bool AuthService::IsConfigured() const {
  std::lock_guard<std::mutex> lock(mu_);
  return !users_.empty();
}

bool AuthService::IsLockedOut(const std::string& username) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = throttle_.find(LowerTrim(username));
  if (it == throttle_.end()) return false;
  return it->second.first >= kMaxLoginFails &&
         std::chrono::steady_clock::now() < it->second.second;
}

std::optional<std::string> AuthService::Login(const std::string& username,
                                              const std::string& password) {
  const std::string key = LowerTrim(username);
  std::lock_guard<std::mutex> lock(mu_);

  const auto it = users_.find(key);
  if (it == users_.end() || !VerifyPassword(password, it->second.password_hash)) {
    auto& t = throttle_[key];
    t.first += 1;
    if (t.first >= kMaxLoginFails) {
      t.second = std::chrono::steady_clock::now() + std::chrono::minutes(kLockoutMinutes);
    }
    return std::nullopt;
  }
  throttle_.erase(key);   // success clears the failure window
  PurgeExpired();

  unsigned char raw[32];
  RAND_bytes(raw, sizeof(raw));
  const std::string token = ToHexLower(raw, sizeof(raw));

  const auto now = std::chrono::steady_clock::now();
  sessions_[token] = SessionInfo{key, it->second.is_admin, it->second.can_transmit, now, now};
  return token;
}

bool AuthService::ValidateSession(const std::string& token) {
  if (token.empty()) return false;
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = sessions_.find(token);
  if (it == sessions_.end()) return false;

  const auto now = std::chrono::steady_clock::now();
  if (now - it->second.last_activity > std::chrono::minutes(session_timeout_minutes_)) {
    sessions_.erase(it);
    return false;
  }
  it->second.last_activity = now;   // sliding window, matches C#
  return true;
}

bool AuthService::IsAdmin(const std::string& token) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = sessions_.find(token);
  return it != sessions_.end() && it->second.is_admin;
}

bool AuthService::CanTransmit(const std::string& token) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = sessions_.find(token);
  return it != sessions_.end() && it->second.can_transmit;
}

std::optional<std::string> AuthService::Username(const std::string& token) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = sessions_.find(token);
  if (it == sessions_.end()) return std::nullopt;
  return it->second.username;
}

void AuthService::Logout(const std::string& token) {
  std::lock_guard<std::mutex> lock(mu_);
  sessions_.erase(token);
}

void AuthService::PurgeExpired() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (now - it->second.last_activity > std::chrono::minutes(session_timeout_minutes_)) {
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

bool AuthService::RemoveUser(const std::string& username) {
  const std::string key = LowerTrim(username);
  std::lock_guard<std::mutex> lock(mu_);
  if (users_.erase(key) == 0) return false;
  // Their sessions go too. Leaving a live session for a deleted account means
  // the account is deleted everywhere except where it matters.
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    it = (it->second.username == key) ? sessions_.erase(it) : std::next(it);
  }
  return true;
}

bool AuthService::ChangePassword(const std::string& username,
                                 const std::string& new_hash) {
  const std::string key = LowerTrim(username);
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = users_.find(key);
  if (it == users_.end()) return false;
  it->second.password_hash = new_hash;
  // ⚠️ Existing sessions are INVALIDATED. A password change that leaves old
  // sessions working does not actually revoke anything, which is usually the
  // whole reason it is being changed.
  for (auto sit = sessions_.begin(); sit != sessions_.end();) {
    sit = (sit->second.username == key) ? sessions_.erase(sit) : std::next(sit);
  }
  return true;
}

bool AuthService::SetCanTransmit(const std::string& username, bool allow) {
  const std::string key = LowerTrim(username);
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = users_.find(key);
  if (it == users_.end()) return false;
  it->second.can_transmit = allow;
  // Live sessions carry their own copy of the flag, so update those too -
  // otherwise revoking transmit rights does nothing until the user logs out.
  for (auto& [token, s] : sessions_) {
    if (s.username == key) s.can_transmit = allow;
  }
  return true;
}

int AuthService::KillUserSessions(const std::string& username) {
  const std::string key = LowerTrim(username);
  std::lock_guard<std::mutex> lock(mu_);
  int n = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (it->second.username == key) {
      it = sessions_.erase(it);
      ++n;
    } else {
      ++it;
    }
  }
  return n;
}

std::vector<AuthService::UserRow> AuthService::ListUsers() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<UserRow> out;
  for (const auto& [name, u] : users_) {
    out.push_back({name, u.is_admin, u.can_transmit});
  }
  return out;
}

std::vector<AuthService::SessionRow> AuthService::ListSessions() const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto now = std::chrono::steady_clock::now();
  std::vector<SessionRow> out;
  for (const auto& [token, s] : sessions_) {
    // ⚠️ Only a PREFIX of the token. A full session token in an admin listing
    // is a credential in a log, a screenshot and a support ticket.
    out.push_back({token.substr(0, 8) + "...", s.username, s.is_admin,
                   s.can_transmit,
                   std::chrono::duration_cast<std::chrono::seconds>(
                       now - s.last_activity).count()});
  }
  return out;
}

int AuthService::AdminCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  int n = 0;
  for (const auto& [name, u] : users_) {
    if (u.is_admin) ++n;
  }
  return n;
}

std::string AuthService::PasswordHashOf(const std::string& username) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = users_.find(LowerTrim(username));
  return it == users_.end() ? std::string() : it->second.password_hash;
}
