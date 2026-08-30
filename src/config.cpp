#include "config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace {

template <typename T>
void Get(const json& j, const char* key, T& out) {
  if (j.contains(key) && !j.at(key).is_null()) out = j.at(key).get<T>();
}

}  // namespace

std::vector<std::string> Config::DefaultPaths() {
  std::vector<std::string> paths;
  if (const char* explicit_path = std::getenv("HAMDECK_CONFIG")) {
    paths.emplace_back(explicit_path);
  }
  if (const char* home = std::getenv("HOME")) {
    paths.emplace_back(std::string(home) + "/.hamdeck/hamdeck.json");
  }
  paths.emplace_back("/etc/hamdeck-cpp/config.json");
  return paths;
}

bool Config::Load(const std::string& path, Config& out, std::string& error) {
  // ⚠️ Parse into a LOCAL and assign only on success. Writing into `out` as we
  // go left a caller who ignored the return value holding a half-applied config -
  // some keys from the file, the rest defaults - which is the worst of both. It
  // also made one of these tests pass for the wrong reason: a rejected value from
  // an earlier load was still present and tripped a different check.
  Config cfg;
  std::ifstream f(path);
  if (!f) {
    error = "not found";
    return false;
  }
  std::stringstream buf;
  buf << f.rdbuf();

  json j;
  try {
    j = json::parse(buf.str(), nullptr, true, /*ignore_comments=*/true);
  } catch (const std::exception& e) {
    error = std::string("parse error: ") + e.what();
    return false;
  }
  if (!j.is_object()) {
    error = "top level is not an object";
    return false;
  }

  try {
    Get(j, "radio_port", cfg.radio_port);
    Get(j, "radio_baud", cfg.radio_baud);
    Get(j, "record_sample_rate", cfg.record_sample_rate);
    Get(j, "alsa_capture_device", cfg.alsa_capture_device);
    Get(j, "alsa_playback_device", cfg.alsa_playback_device);
    Get(j, "audio_stream_enabled", cfg.audio_stream_enabled);
    Get(j, "tx_audio_enabled", cfg.tx_audio_enabled);
    Get(j, "api_port", cfg.api_port);
    Get(j, "dashboard_port", cfg.dashboard_port);
    Get(j, "api_bind_lan", cfg.api_bind_lan);
    Get(j, "allow_anonymous_status", cfg.allow_anonymous_status);
    Get(j, "ptt_timeout_seconds", cfg.ptt_timeout_seconds);
    Get(j, "web_session_timeout", cfg.web_session_timeout);
    Get(j, "admin_only_login", cfg.admin_only_login);
    Get(j, "tgxl_host", cfg.tgxl_host);
    Get(j, "tgxl_port", cfg.tgxl_port);
    Get(j, "kmtronic_host", cfg.kmtronic_host);
    Get(j, "kmtronic_port", cfg.kmtronic_port);

    if (j.contains("web_users") && j.at("web_users").is_array()) {
      cfg.web_users.clear();
      for (const auto& u : j.at("web_users")) {
        ConfigUser cu;
        Get(u, "username", cu.username);
        Get(u, "password_hash", cu.password_hash);
        Get(u, "is_admin", cu.is_admin);
        Get(u, "can_transmit", cu.can_transmit);
        if (cu.username.empty() || cu.password_hash.empty()) {
          // A user entry that cannot authenticate is a mistake, not a disabled
          // account. Refuse the file rather than start with a user list that is
          // quietly shorter than the one on disk.
          error = "web_users entry missing username or password_hash";
          return false;
        }
        cfg.web_users.push_back(std::move(cu));
      }
    }
  } catch (const std::exception& e) {
    error = std::string("bad value: ") + e.what();
    return false;
  }

  // ⚠️ A timeout of zero is a legitimate "disabled", but a NEGATIVE one is a
  // typo, and treating it as disabled would silently remove the only thing
  // stopping a stuck PTT.
  if (cfg.ptt_timeout_seconds < 0) {
    error = "ptt_timeout_seconds must be >= 0 (0 disables the watchdog)";
    return false;
  }
  if (cfg.api_port == cfg.dashboard_port) {
    error = "api_port and dashboard_port must differ";
    return false;
  }
  out = std::move(cfg);   // only now
  return true;
}
