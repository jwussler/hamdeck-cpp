#include "config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
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
  // ⚠️ "MISSING" AND "UNREADABLE" ARE NOT THE SAME THING.
  // A missing config is fine - the defaults are usable. A config that EXISTS but
  // cannot be read is a permissions mistake, and treating it as absent means the
  // station quietly runs on defaults while the operator's real settings sit on
  // disk being ignored. That is exactly the failure the malformed-file rule
  // exists to prevent, arriving through a different door.
  //
  // Found on deployment: the service ran as one user with the config owned 640
  // by another, came up on the SIMULATOR with no users configured, and said
  // nothing about it.
  {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (!exists) {
      error = "not found";
      return false;
    }
  }
  std::ifstream f(path);
  if (!f) {
    error = "exists but cannot be read - check ownership and permissions";
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
    Get(j, "record_path", cfg.record_path);
    Get(j, "record_buffer_seconds", cfg.record_buffer_seconds);
    Get(j, "record_max_seconds", cfg.record_max_seconds);
    Get(j, "record_warning_seconds", cfg.record_warning_seconds);
    Get(j, "api_port", cfg.api_port);
    Get(j, "cat_proxy_port", cfg.cat_proxy_port);
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
        // Absent in a config written before station rights existed, and Get
        // leaves the default alone - so an upgraded host grants nobody this.
        Get(u, "is_station", cu.is_station);
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
  if (cfg.cat_proxy_port != 0 &&
      (cfg.cat_proxy_port == cfg.api_port || cfg.cat_proxy_port == cfg.dashboard_port)) {
    error = "cat_proxy_port must differ from api_port and dashboard_port";
    return false;
  }
  if (cfg.api_port == cfg.dashboard_port) {
    error = "api_port and dashboard_port must differ";
    return false;
  }
  out = std::move(cfg);   // only now
  return true;
}

bool Config::Save(const std::string& path, std::string& error) const {
  // Start from what is already on disk so unknown keys survive.
  json j = json::object();
  {
    std::ifstream in(path);
    if (in) {
      std::stringstream buf;
      buf << in.rdbuf();
      try {
        j = json::parse(buf.str(), nullptr, true, /*ignore_comments=*/true);
      } catch (const std::exception&) {
        // A file we could not parse is not a file to merge into: refuse rather
        // than overwrite something the operator may still want to fix by hand.
        error = "existing config is not valid JSON - refusing to overwrite it";
        return false;
      }
      if (!j.is_object()) j = json::object();
    }
  }

  j["radio_port"] = radio_port;
  j["radio_baud"] = radio_baud;
  j["record_sample_rate"] = record_sample_rate;
  j["record_path"] = record_path;
  j["record_buffer_seconds"] = record_buffer_seconds;
  j["record_max_seconds"] = record_max_seconds;
  j["alsa_capture_device"] = alsa_capture_device;
  j["alsa_playback_device"] = alsa_playback_device;
  j["api_port"] = api_port;
  j["cat_proxy_port"] = cat_proxy_port;
  j["dashboard_port"] = dashboard_port;
  j["allow_anonymous_status"] = allow_anonymous_status;
  j["ptt_timeout_seconds"] = ptt_timeout_seconds;
  j["web_session_timeout"] = web_session_timeout;
  j["admin_only_login"] = admin_only_login;

  json users = json::array();
  for (const auto& u : web_users) {
    users.push_back({{"username", u.username},
                     {"password_hash", u.password_hash},
                     {"is_admin", u.is_admin},
                     {"can_transmit", u.can_transmit},
                     {"is_station", u.is_station}});
  }
  j["web_users"] = users;

  // ⚠️ Temp file then rename. A crash or a full disk halfway through a direct
  // write leaves a truncated config, and the host refuses to start on a config
  // it cannot parse - which is correct, and would be a bad way to discover it.
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) {
      error = "cannot write " + tmp;
      return false;
    }
    out << j.dump(2) << '\n';
    if (!out) {
      error = "write failed";
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    error = "could not replace " + path;
    return false;
  }
  return true;
}
