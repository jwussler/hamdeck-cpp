#include "api.h"

#include <chrono>
#include <format>
#include <iostream>
#include <set>
#include <thread>

#include "version.h"

namespace {

// The only routes an unauthenticated caller ever gets, whatever the settings:
// enough to learn the service is alive and to log in. Everything else -
// including the frequency, the meters and the receiver audio - is somebody's
// operating activity. Mirrors AlwaysAnonymousRoutes in the C# ApiServer.
const std::set<std::string> kAlwaysAnonymous = {
    "/api/health", "/api/auth/status",
};

// Read rig state without changing it. Anonymous ONLY when allow_anonymous_status
// is set. Mirrors ReadOnlyRoutes in the C# ApiServer.
const std::set<std::string> kReadOnly = {
    "/api/status", "/api/status/full", "/api/health", "/api/meters",
    "/api/session", "/api/cluster/spots", "/api/record/status",
    "/api/freq", "/api/freq-b", "/api/freq/get", "/api/volume/get",
    "/api/cw-speed/get", "/api/rf-gain/get", "/api/ant/get", "/api/ant/rx/get",
    "/api/auth/status", "/api/power/limit", "/api/vfo-lock/status",
    "/api/diversity/status",
};

std::string JsonBool(bool b) { return b ? "true" : "false"; }

// Order matters and matches the C# GetSessionToken: cookie, then Bearer, then
// ?token=. The query parameter exists because a browser cannot set headers on a
// WebSocket handshake.
std::string ExtractToken(const httplib::Request& req) {
  if (req.has_header("Cookie")) {
    const std::string cookies = req.get_header_value("Cookie");
    const std::string needle = "hamdeck_session=";
    if (const auto p = cookies.find(needle); p != std::string::npos) {
      const auto start = p + needle.size();
      const auto end = cookies.find(';', start);
      return cookies.substr(start, end == std::string::npos ? std::string::npos
                                                            : end - start);
    }
  }
  if (req.has_header("Authorization")) {
    const std::string h = req.get_header_value("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
  }
  if (req.has_param("token")) return req.get_param_value("token");
  return "";
}

void WriteJson(httplib::Response& res, int status, const std::string& body) {
  res.status = status;
  res.set_content(body, "application/json");
}

std::string StatusJson(const ApiDeps& deps) {
  if (!deps.poller) return R"({"connected":false,"stale":true})";
  const RigSnapshot s = deps.poller->Snapshot();
  const long long age = deps.poller->CacheAgeMs();
  const bool stale = (age < 0) || (age > RadioPoller::kStaleAfterMs);
  return std::format(
      R"({{"connected":{},"freq":{},"mode":"{}","vfo":"{}","power":{},"tx":{},)"
      R"("tx_timeout_in":0,"split":{},"amp_tuning":false,"tgxl_tuning":false,)"
      R"("freq_buffer":"","vfo_locked":{},"diversity":false,"cache_age_ms":{},"stale":{}}})",
      JsonBool(s.connected), s.freq, s.mode, s.vfo, s.power, JsonBool(s.tx),
      JsonBool(s.split), JsonBool(s.vfo_locked), age < 0 ? 0 : age, JsonBool(stale));
}

std::string HealthJson(const ApiDeps& deps, int bound_port) {
  const bool connected = deps.poller && deps.poller->Snapshot().connected;
  return std::format(
      R"({{"status":"ok","service":"{}","version":"{}","port":{},)"
      R"("rig_connected":{},"amp_tuning":false,"tgxl_tuning":false,"freq_buffer":""}})",
      kServiceName, kVersion, bound_port, JsonBool(connected));
}

// Minimal field grab. Good enough for the two-field login body and nothing more;
// it is replaced the moment any route needs real JSON input.
std::string JsonField(const std::string& body, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  auto p = body.find(needle);
  if (p == std::string::npos) return "";
  p = body.find(':', p + needle.size());
  if (p == std::string::npos) return "";
  p = body.find('"', p);
  if (p == std::string::npos) return "";
  const auto end = body.find('"', p + 1);
  if (end == std::string::npos) return "";
  return body.substr(p + 1, end - p - 1);
}

}  // namespace

void InstallRoutes(httplib::Server& server, Listener listener, int bound_port,
                   const ApiDeps& deps) {
  const bool trusted = (listener == Listener::kControl);
  const char* role = trusted ? "control" : "dash";

  // ── The auth gate ──────────────────────────────────────────────────────────
  // Runs before every route. Default is DENY: a route added later is protected
  // unless somebody deliberately lists it as anonymous. The opposite default -
  // open unless remembered - is the shape that leaks, because it fails open
  // every time somebody forgets.
  server.set_pre_routing_handler(
      [&deps, trusted, role](const httplib::Request& req, httplib::Response& res) {
        std::cout << role << ' ' << req.method << ' ' << req.path << '\n' << std::flush;

        if (trusted) return httplib::Server::HandlerResponse::Unhandled;
        if (kAlwaysAnonymous.count(req.path)) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (deps.allow_anonymous_status && kReadOnly.count(req.path)) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (req.path == "/api/auth/login") {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        if (deps.auth && deps.auth->ValidateSession(ExtractToken(req))) {
          return httplib::Server::HandlerResponse::Unhandled;
        }
        WriteJson(res, 401, R"({"status":"error","message":"Authentication required"})");
        return httplib::Server::HandlerResponse::Handled;
      });

  server.Get("/api/health", [&deps, bound_port](const httplib::Request&, httplib::Response& res) {
    WriteJson(res, 200, HealthJson(deps, bound_port));
  });

  server.Get("/api/status", [&deps](const httplib::Request&, httplib::Response& res) {
    WriteJson(res, 200, StatusJson(deps));
  });

  server.Get("/api/auth/status", [&deps, trusted](const httplib::Request& req, httplib::Response& res) {
    const std::string token = ExtractToken(req);
    const bool ok = deps.auth && deps.auth->ValidateSession(token);
    const auto user = ok ? deps.auth->Username(token) : std::nullopt;
    WriteJson(res, 200,
              std::format(
                  R"({{"status":"ok","authenticated":{},"is_admin":{},"can_transmit":{},)"
                  R"("username":{},"token":null}})",
                  JsonBool(ok || trusted),
                  JsonBool(ok && deps.auth->IsAdmin(token)),
                  JsonBool(ok && deps.auth->CanTransmit(token)),
                  user ? "\"" + *user + "\"" : "null"));
  });

  server.Post("/api/auth/login", [&deps](const httplib::Request& req, httplib::Response& res) {
    if (!deps.auth) {
      WriteJson(res, 400, R"({"status":"error","message":"Invalid request"})");
      return;
    }
    const std::string user = JsonField(req.body, "username");
    const std::string pass = JsonField(req.body, "password");

    // Every rejection sleeps before answering, so "no such user" and "wrong
    // password" cost the same wall-clock time and guesses cannot be pipelined.
    if (deps.auth->IsLockedOut(user)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(AuthService::kFailureDelayMs));
      WriteJson(res, 429,
                R"({"status":"error","message":"Too many failed attempts. Try again in a few minutes."})");
      return;
    }
    const auto token = deps.auth->Login(user, pass);
    if (!token) {
      std::this_thread::sleep_for(std::chrono::milliseconds(AuthService::kFailureDelayMs));
      WriteJson(res, 401, R"({"status":"error","message":"Invalid credentials"})");
      return;
    }

    // The token goes in the cookie and NOT in the body - CARRYOVER.md section 2.
    // HttpOnly keeps it away from page scripts; SameSite=Strict closes the
    // top-level-navigation CSRF vector on state-changing GETs, of which this API
    // has many (/api/ptt/on is a GET).
    res.set_header("Set-Cookie",
                   std::format("hamdeck_session={}; Path=/; HttpOnly; SameSite=Strict; Max-Age={}",
                               *token, deps.auth->session_timeout_minutes() * 60));
    WriteJson(res, 200, R"({"status":"ok","message":"Login successful"})");
  });

  server.Post("/api/auth/logout", [&deps](const httplib::Request& req, httplib::Response& res) {
    if (deps.auth) deps.auth->Logout(ExtractToken(req));
    res.set_header("Set-Cookie", "hamdeck_session=; Path=/; HttpOnly; Max-Age=0");
    WriteJson(res, 200, R"({"status":"ok","message":"Logged out"})");
  });
}
