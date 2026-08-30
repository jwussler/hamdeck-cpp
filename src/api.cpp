#include "api.h"

#include <chrono>
#include <format>
#include <iostream>
#include <functional>
#include <set>
#include <vector>
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
std::string ExtractToken(const HttpRequest& req) {
  {
    const std::string cookies = req.Header("cookie");
    const std::string needle = "hamdeck_session=";
    if (const auto p = cookies.find(needle); p != std::string::npos) {
      const auto start = p + needle.size();
      const auto end = cookies.find(';', start);
      return cookies.substr(start, end == std::string::npos ? std::string::npos
                                                            : end - start);
    }
  }
  {
    const std::string h = req.Header("authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
  }
  return req.QueryParam("token");
}

void WriteJson(HttpResponse& res, int status, const std::string& body) {
  res.status = status;
  res.body = body;
  res.content_type = "application/json";
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

// Field order matches the reference host exactly. Order is not required by JSON,
// but a diff of two captures is far easier to read when it does.
std::string StatusFullJson(const ApiDeps& deps) {
  if (!deps.poller) return "{}";
  const RigSnapshot s = deps.poller->Snapshot();
  return std::format(
      R"({{"ant":{},"rxant":{},"nb":{},"nr":{},"notch":{},"lock":{},"preamp":{},)"
      R"("att":{},"agc":"{}","vox":{},"comp":{},"mon":{},"rit":{},"rit_offset":{},)"
      R"("xit":{},"freq_b":{},"rf_gain":{},"rxant_km":0}})",
      s.ant, JsonBool(s.rxant), JsonBool(s.nb), JsonBool(s.nr), JsonBool(s.notch),
      JsonBool(s.vfo_locked), s.preamp, JsonBool(s.att), s.agc, JsonBool(s.vox),
      JsonBool(s.comp), JsonBool(s.mon), JsonBool(s.rit), s.rit_offset,
      JsonBool(s.xit), s.freq_b, s.rf_gain);
}

std::string MetersJson(const ApiDeps& deps) {
  if (!deps.poller) return R"({"status":"ok","s_meter":0,"swr":0,"alc":0,"power":0})";
  const RigSnapshot s = deps.poller->Snapshot();
  return std::format(
      R"({{"status":"ok","s_meter":{},"swr":{},"alc":{},"power":{}}})",
      s.s_meter, s.swr, s.alc, s.power_mtr);
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

std::string OkJson(const std::string& key, const std::string& val_json) {
  return std::format(R"({{"status":"ok","{}":{}}})", key, val_json);
}

std::string Quoted(const std::string& s) { return "\"" + s + "\""; }

// FTDX-101 MD codes, the inverse of ModeName().
int ModeCode(const std::string& name) {
  if (name == "LSB") return 1;
  if (name == "USB") return 2;
  if (name == "CW")  return 3;
  if (name == "FM")  return 4;
  if (name == "AM")  return 5;
  if (name == "DATA-U") return 9;   // the C# /api/mode/data maps here
  return 0;
}

std::string Pad(long long v, int width) {
  std::string s = std::to_string(v);
  return std::string(width > (int)s.size() ? width - s.size() : 0, '0') + s;
}

// ⚠️ POWER CAP, PORTED FAITHFULLY AND DELIBERATELY NOT "FIXED".
// In the C# host a LOCAL caller is capped at 100 W while a remote caller gets
// 200 W - which reads backwards, so it is exactly the kind of thing to port
// as-is and ask about rather than quietly invert. Flagged in WIP.md.
constexpr int kLocalPowerCap = 100;
constexpr int kMaxWatts      = 200;

}  // namespace

void InstallRoutes(HttpServer& server, Listener listener, int bound_port,
                   const ApiDeps& deps) {
  const bool trusted = (listener == Listener::kControl);
  const char* role = trusted ? "control" : "dash";

  // ── The auth gate ──────────────────────────────────────────────────────────
  // Runs before every route. Default is DENY: a route added later is protected
  // unless somebody deliberately lists it as anonymous. The opposite default -
  // open unless remembered - is the shape that leaks, because it fails open
  // every time somebody forgets.
  server.SetPreRouting(
      [&deps, trusted, role](const HttpRequest& req, HttpResponse& res) -> bool {
        std::cout << role << ' ' << req.method << ' ' << req.path << '\n' << std::flush;

        if (trusted) return true;
        if (kAlwaysAnonymous.count(req.path)) return true;
        if (deps.allow_anonymous_status && kReadOnly.count(req.path)) return true;
        if (req.path == "/api/auth/login") return true;
        if (deps.auth && deps.auth->ValidateSession(ExtractToken(req))) return true;
        WriteJson(res, 401, R"({"status":"error","message":"Authentication required"})");
        return false;
      });

  server.Get("/api/health", [&deps, bound_port](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, HealthJson(deps, bound_port));
  });

  server.Get("/api/status", [&deps](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, StatusJson(deps));
  });

  server.Get("/api/status/full", [&deps](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, StatusFullJson(deps));
  });

  server.Get("/api/meters", [&deps](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, MetersJson(deps));
  });

  server.Get("/api/auth/status", [&deps, trusted](const HttpRequest& req, HttpResponse& res) {
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

  server.Post("/api/auth/login", [&deps](const HttpRequest& req, HttpResponse& res) {
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
    res.extra_headers.push_back(
        {"Set-Cookie",
         std::format("hamdeck_session={}; Path=/; HttpOnly; SameSite=Strict; Max-Age={}",
                     *token, deps.auth->session_timeout_minutes() * 60)});
    WriteJson(res, 200, R"({"status":"ok","message":"Login successful"})");
  });

  // ── The declarative table ──────────────────────────────────────────────────
  // Most of the 141 C# routes are one CAT verb each. They belong in a table, not
  // in 141 hand-written functions: a table is auditable at a glance, and adding a
  // route cannot accidentally skip the auth gate, which already ran above.
  //
  // Handlers NEVER touch the serial port. They queue a command (drained by the
  // poller thread) and read state from the cache. See RadioPoller::Enqueue.
  struct Route {
    const char* path;
    std::function<std::string(bool is_local)> handler;
  };

  RadioPoller* rig = deps.poller;
  auto set_mode = [rig](const char* name) {
    return [rig, name](bool) {
      if (rig) rig->Enqueue(std::format("MD0{};", ModeCode(name)));
      return OkJson("mode", Quoted(name));
    };
  };
  auto set_power = [rig](const char* label, int watts) {
    return [rig, label, watts](bool) {
      if (rig) rig->Enqueue("PC" + Pad(watts, 3) + ";");
      return std::format(R"({{"status":"ok","power":"{}","watts":{}}})", label, watts);
    };
  };

  const std::vector<Route> table = {
      {"/api/test", [](bool) { return std::string(R"({"ok":true,"message":"API is working"})"); }},

      {"/api/mode/usb",  set_mode("USB")},
      {"/api/mode/lsb",  set_mode("LSB")},
      {"/api/mode/cw",   set_mode("CW")},
      {"/api/mode/am",   set_mode("AM")},
      {"/api/mode/fm",   set_mode("FM")},
      {"/api/mode/data", set_mode("DATA-U")},

      {"/api/vfo/a", [rig](bool) { if (rig) rig->Enqueue("VS0;"); return OkJson("vfo", Quoted("A")); }},
      {"/api/vfo/b", [rig](bool) { if (rig) rig->Enqueue("VS1;"); return OkJson("vfo", Quoted("B")); }},

      {"/api/split/on",  [rig](bool) { if (rig) rig->Enqueue("ST1;"); return OkJson("split", "1"); }},
      {"/api/split/off", [rig](bool) { if (rig) rig->Enqueue("ST0;"); return OkJson("split", "0"); }},
      {"/api/split/toggle", [rig](bool) {
         const bool now = rig && rig->Snapshot().split;
         if (rig) rig->Enqueue(now ? "ST0;" : "ST1;");
         return OkJson("split", now ? "false" : "true");
       }},

      {"/api/lock/on",  [rig](bool) { if (rig) rig->Enqueue("LK1;"); return OkJson("lock", "1"); }},
      {"/api/lock/off", [rig](bool) { if (rig) rig->Enqueue("LK0;"); return OkJson("lock", "0"); }},

      // ⚠️ PTT ON is here; PTT OFF IS NOT. Unkeying must wait for the audio still
      // queued in the ALSA buffer or the tail of every transmission is lost
      // (CARRYOVER.md section 4a), and that wait needs the real device depth from
      // /proc/asound. Shipping an unkey that drops PTT immediately would look
      // like it works and quietly cut the end off every over - the exact bug that
      // took a report from a net to find. It lands with the audio work.
      {"/api/ptt/on",  [rig](bool) { if (rig) rig->Enqueue("TX1;"); return OkJson("ptt", "1"); }},
      {"/api/ptt/key", [rig](bool) { if (rig) rig->Enqueue("TX1;"); return OkJson("ptt", "1"); }},

      {"/api/power/limit", [](bool is_local) {
         return std::format(R"({{"status":"ok","max_watts":{},"is_local":{}}})",
                            is_local ? kLocalPowerCap : kMaxWatts, JsonBool(is_local));
       }},
      {"/api/power/qrp",  set_power("qrp", 5)},
      {"/api/power/low",  set_power("low", 25)},
      {"/api/power/mid",  set_power("mid", 50)},
      {"/api/power/high", set_power("high", 100)},
      {"/api/power/max",  [rig](bool is_local) {
         const int w = is_local ? kLocalPowerCap : kMaxWatts;
         if (rig) rig->Enqueue("PC" + Pad(w, 3) + ";");
         return std::format(R"({{"status":"ok","power":"{}","watts":{},"clamped":{}}})",
                            is_local ? "high" : "max", w, JsonBool(is_local));
       }},

      {"/api/freq",   [rig](bool) {
         return std::format(R"({{"freq":{}}})", rig ? rig->Snapshot().freq : 0); }},
      {"/api/freq-b", [rig](bool) {
         return std::format(R"({{"freq_b":{}}})", rig ? rig->Snapshot().freq_b : 0); }},
  };

  for (const auto& route : table) {
    server.Get(route.path, [h = route.handler, trusted](const HttpRequest&, HttpResponse& res) {
      WriteJson(res, 200, h(trusted));
    });
  }

  // ── RX audio ───────────────────────────────────────────────────────────────
  // The auth gate already ran on the upgrade: an unauthenticated caller never
  // reaches here, so the stream cannot be listened to without a session.
  if (deps.rx_audio) {
    RxAudioStream* rx = deps.rx_audio;
    server.WebSocketRoute(
        "/ws",
        [rx](const HttpRequest&, std::shared_ptr<WsConnection> c) {
          // The config frame goes FIRST, before any binary frame, or the client
          // has no way to know how to interpret the bytes it is about to get.
          c->SendText(rx->ConfigJson());
          rx->AddClient(std::move(c));
        },
        [rx](std::shared_ptr<WsConnection> c) { rx->RemoveClient(c); });
  }

  server.Post("/api/auth/logout", [&deps](const HttpRequest& req, HttpResponse& res) {
    if (deps.auth) deps.auth->Logout(ExtractToken(req));
    res.extra_headers.push_back({"Set-Cookie", "hamdeck_session=; Path=/; HttpOnly; Max-Age=0"});
    WriteJson(res, 200, R"({"status":"ok","message":"Logged out"})");
  });
}
