#include "api.h"

#include <chrono>
#include <format>
#include <iostream>
#include <cstring>
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

  // ⚠️ EXISTS SO DESTRUCTIVE TOOLING CAN FAIL CLOSED.
  // A tool that walks every route - including the ones that key the transmitter -
  // must be able to prove it is talking to a simulator, not the station. Asking
  // the operator to point it at the right host is not a safeguard; a machine-
  // checkable answer is. The reference host does not serve this route, so it
  // 404s there and any such tool must refuse on 404 rather than assume.
  server.Get("/api/backend", [&deps](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","cat":"{}","simulated":{}}})",
                          deps.poller ? deps.poller->Backend() : "none",
                          JsonBool(deps.simulated)));
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

      {"/api/cw-speed/get", [rig](bool) {
         return std::format(R"({{"status":"ok","wpm":{}}})", rig ? rig->Snapshot().cw_speed : 0); }},
      {"/api/ant/get", [rig](bool) {
         return std::format(R"({{"status":"ok","ant":{}}})", rig ? rig->Snapshot().ant : 0); }},
      {"/api/ant/rx/get", [rig](bool) {
         return std::format(R"({{"status":"ok","rxant":{}}})",
                            JsonBool(rig && rig->Snapshot().rxant)); }},
      {"/api/rf-gain/get", [rig](bool) {
         return std::format(R"({{"status":"ok","rf_gain":{}}})",
                            rig ? rig->Snapshot().rf_gain : 0); }},

      {"/api/freq",   [rig](bool) {
         return std::format(R"({{"freq":{}}})", rig ? rig->Snapshot().freq : 0); }},
      {"/api/freq-b", [rig](bool) {
         return std::format(R"({{"freq_b":{}}})", rig ? rig->Snapshot().freq_b : 0); }},
  };

  // ── Flag groups, generated rather than hand-written ────────────────────────
  // Each of these is the same three routes over a different CAT verb. Sixty
  // near-identical handlers is sixty chances to paste the wrong key; a spec table
  // makes a mismatch visible on one line.
  //
  // ⚠️ on/off answer with 1/0 and toggle answers with true/false. That is not a
  // tidy-up opportunity - it is what the C# host emits and what the existing
  // client parses.
  struct FlagSpec {
    const char* path;                          // e.g. "/api/nb"
    const char* key;                           // e.g. "nb"
    const char* cat;                           // e.g. "NB0"
    bool RigSnapshot::* field;
  };
  static const FlagSpec kFlags[] = {
      {"/api/nb",     "nb",    "NB0", &RigSnapshot::nb},
      {"/api/nr",     "nr",    "NR0", &RigSnapshot::nr},
      // NOT "BP0" - the real verb is BC0, and it takes a two-digit value:
      // BC01;/BC00;. Handled by the two-digit spec below, not here.
      {"/api/att",    "att",   "RA0", &RigSnapshot::att},
      {"/api/vox",    "vox",   "VX",  &RigSnapshot::vox},
      {"/api/comp",   "comp",  "PR0", &RigSnapshot::comp},
      // NOT a simple ML0<0|1>; - monitor is ML0000;/ML0001; and turning it back on
      // also restores the saved level. Handled separately below.
      {"/api/rit",    "rit",   "RT",  &RigSnapshot::rit},
      {"/api/xit",    "xit",   "XT",  &RigSnapshot::xit},
      {"/api/lock",   "lock",  "LK",  &RigSnapshot::vfo_locked},
      // NOT "AR" - RX antenna is a MENU item, EX030103<0|1>;. Below.
  };

  std::vector<Route> generated;
  for (const auto& f : kFlags) {
    const std::string key = f.key, cat = f.cat;
    generated.push_back({strdup((std::string(f.path) + "/on").c_str()),
                         [rig, cat, key](bool) {
                           if (rig) rig->Enqueue(cat + "1;");
                           return OkJson(key, "1");
                         }});
    generated.push_back({strdup((std::string(f.path) + "/off").c_str()),
                         [rig, cat, key](bool) {
                           if (rig) rig->Enqueue(cat + "0;");
                           return OkJson(key, "0");
                         }});
    auto field = f.field;
    generated.push_back({strdup((std::string(f.path) + "/toggle").c_str()),
                         [rig, cat, key, field](bool) {
                           const bool now = rig && (rig->Snapshot().*field);
                           if (rig) rig->Enqueue(cat + (now ? "0;" : "1;"));
                           return OkJson(key, now ? "false" : "true");
                         }});
  }

  // Notch: BC0 takes a two-digit value, so it cannot share the one-digit path.
  {
    auto notch_set = [rig](bool on) {
      return [rig, on](bool) {
        if (rig) rig->Enqueue(on ? "BC01;" : "BC00;");
        return OkJson("notch", on ? "1" : "0");
      };
    };
    generated.push_back({"/api/notch/on",  notch_set(true)});
    generated.push_back({"/api/notch/off", notch_set(false)});
    generated.push_back({"/api/notch/toggle", [rig](bool) {
        const bool now = rig && rig->Snapshot().notch;
        if (rig) rig->Enqueue(now ? "BC00;" : "BC01;");
        return OkJson("notch", now ? "false" : "true"); }});
  }

  // Monitor: ML0000;/ML0001;. Turning it ON also restores a level, which the
  // reference host tracks; until that state is carried here, only the on/off
  // half is implemented and the level is left alone.
  {
    generated.push_back({"/api/mon/on", [rig](bool) {
        if (rig) rig->Enqueue("ML0001;");
        return OkJson("mon", "1"); }});
    generated.push_back({"/api/mon/off", [rig](bool) {
        if (rig) rig->Enqueue("ML0000;");
        return OkJson("mon", "0"); }});
    generated.push_back({"/api/mon/toggle", [rig](bool) {
        const bool now = rig && rig->Snapshot().mon;
        if (rig) rig->Enqueue(now ? "ML0000;" : "ML0001;");
        return OkJson("mon", now ? "false" : "true"); }});
  }

  // RX antenna is a MENU item, not a CAT flag: EX030103<0|1>;
  {
    generated.push_back({"/api/ant/rx/on", [rig](bool) {
        if (rig) rig->Enqueue("EX0301031;");
        return OkJson("rxant", "1"); }});
    generated.push_back({"/api/ant/rx/off", [rig](bool) {
        if (rig) rig->Enqueue("EX0301030;");
        return OkJson("rxant", "0"); }});
    generated.push_back({"/api/ant/rx/toggle", [rig](bool) {
        const bool now = rig && rig->Snapshot().rxant;
        if (rig) rig->Enqueue(now ? "EX0301030;" : "EX0301031;");
        return OkJson("rxant", now ? "false" : "true"); }});
  }

  // The /api/toggle/* aliases the client also uses. /api/toggle/dnr and
  // /api/toggle/nr are the same route under two names in the C# host; both are
  // kept, because dropping an alias breaks whichever client happens to use it.
  struct AliasSpec { const char* path; const char* key; const char* cat;
                     bool RigSnapshot::* field; };
  static const AliasSpec kToggleAliases[] = {
      {"/api/toggle/nb",    "nb",    "NB0", &RigSnapshot::nb},
      {"/api/toggle/nr",    "nr",    "NR0", &RigSnapshot::nr},
      {"/api/toggle/dnr",   "nr",    "NR0", &RigSnapshot::nr},

      {"/api/toggle/lock",  "lock",  "LK",  &RigSnapshot::vfo_locked},
  };
  generated.push_back({"/api/toggle/notch", [rig](bool) {
      const bool now = rig && rig->Snapshot().notch;
      if (rig) rig->Enqueue(now ? "BC00;" : "BC01;");
      return OkJson("notch", now ? "false" : "true"); }});

  for (const auto& a : kToggleAliases) {
    const std::string key = a.key, cat = a.cat;
    auto field = a.field;
    generated.push_back({a.path, [rig, cat, key, field](bool) {
                           const bool now = rig && (rig->Snapshot().*field);
                           if (rig) rig->Enqueue(cat + (now ? "0;" : "1;"));
                           return OkJson(key, now ? "false" : "true");
                         }});
  }

  // AGC, antenna, width, RIT nudges, CW speed - same idea, different shapes.
  struct AgcSpec { const char* path; const char* name; int code; };
  static const AgcSpec kAgc[] = {
      {"/api/agc/off", "OFF", 0}, {"/api/agc/fast", "FAST", 1},
      {"/api/agc/mid", "MID", 2}, {"/api/agc/slow", "SLOW", 3},
      {"/api/agc/auto", "AUTO", 4},
  };
  for (const auto& a : kAgc) {
    const std::string name = a.name;
    const int code = a.code;
    generated.push_back({a.path, [rig, name, code](bool) {
                           if (rig) rig->Enqueue(std::format("GT0{};", code));
                           return OkJson("agc", Quoted(name));
                         }});
  }
  generated.push_back({"/api/agc/cycle", [rig](bool) {
      const std::string cur = rig ? rig->Snapshot().agc : "AUTO";
      // Cycle order copied from the C# host, including that AUTO goes to OFF.
      const std::string next = cur == "FAST" ? "MID" : cur == "MID" ? "SLOW"
                             : cur == "SLOW" ? "AUTO" : cur == "AUTO" ? "OFF" : "FAST";
      const int code = next == "OFF" ? 0 : next == "FAST" ? 1 : next == "MID" ? 2
                     : next == "SLOW" ? 3 : 4;
      if (rig) rig->Enqueue(std::format("GT0{};", code));
      return OkJson("agc", Quoted(next));
    }});

  for (int n = 1; n <= 3; ++n) {
    generated.push_back({strdup(std::format("/api/ant/{}", n).c_str()),
                         [rig, n](bool) {
                           if (rig) rig->Enqueue(std::format("AN0{};", n));
                           return OkJson("ant", std::to_string(n));
                         }});
  }
  generated.push_back({"/api/ant/toggle", [rig](bool) {
      const int cur = rig ? rig->Snapshot().ant : 1;
      const int next = cur >= 3 ? 1 : cur + 1;
      if (rig) rig->Enqueue(std::format("AN0{};", next));
      return OkJson("ant", std::to_string(next));
    }});

  struct WidthSpec { const char* path; const char* name; int idx; int hz; };
  static const WidthSpec kWidths[] = {
      {"/api/width/narrow", "narrow", 6, 1800},
      {"/api/width/medium", "medium", 10, 2400},
      {"/api/width/wide",   "wide",   14, 3000},
  };
  for (const auto& w : kWidths) {
    const std::string name = w.name;
    const int idx = w.idx, hz = w.hz;
    generated.push_back({w.path, [rig, name, idx, hz](bool) {
                           if (rig) rig->Enqueue(std::format("SH00{:02d};", idx));   // SH00<nn>, verified against the driver
                           return std::format(R"({{"status":"ok","width":"{}","hz":{}}})", name, hz);
                         }});
  }

  // RU/RD carry a FOUR-DIGIT offset - a bare "RU;" is not a command. The
  // reference host nudges by 100 Hz from the current offset and picks the verb
  // by sign.
  auto rit_nudge = [rig](int delta) {
    return [rig, delta](bool) {
      const int next = (rig ? rig->Snapshot().rit_offset : 0) + delta;
      if (rig) {
        rig->Enqueue(next >= 0 ? std::format("RU{:04d};", next)
                               : std::format("RD{:04d};", -next));
      }
      return OkJson("action", Quoted(delta > 0 ? "up" : "down"));
    };
  };
  generated.push_back({"/api/rit/up",   rit_nudge(100)});
  generated.push_back({"/api/rit/down", rit_nudge(-100)});
  generated.push_back({"/api/rit/clear", [rig](bool) {
      if (rig) rig->Enqueue("RC;"); return OkJson("action", Quoted("clear")); }});

  generated.push_back({"/api/cw-speed/up", [rig](bool) {
      const int w = (rig ? rig->Snapshot().cw_speed : 20) + 2;
      if (rig) rig->Enqueue(std::format("KS{:03d};", w));
      return OkJson("wpm", std::to_string(w)); }});
  generated.push_back({"/api/cw-speed/down", [rig](bool) {
      const int w = (rig ? rig->Snapshot().cw_speed : 20) - 2;
      if (rig) rig->Enqueue(std::format("KS{:03d};", w));
      return OkJson("wpm", std::to_string(w)); }});

  generated.push_back({"/api/preamp/on", [rig](bool) {
      if (rig) rig->Enqueue("PA01;"); return OkJson("preamp", "1"); }});
  generated.push_back({"/api/preamp/off", [rig](bool) {
      if (rig) rig->Enqueue("PA00;"); return OkJson("preamp", "0"); }});
  generated.push_back({"/api/preamp/cycle", [rig](bool) {
      const int cur = rig ? rig->Snapshot().preamp : 0;
      if (rig) rig->Enqueue(std::format("PA0{};", cur >= 2 ? 0 : cur + 1));
      return OkJson("action", Quoted("cycle")); }});

  generated.push_back({"/api/vfo/swap", [rig](bool) {
      if (rig) rig->Enqueue("SV;"); return OkJson("action", Quoted("swap")); }});
  // ⚠️ /api/vfo-copy/{a2b,b2a} are NOT implemented, and there is no CAT verb for
  // them. The reference host does it as a read-modify-write sequence: select the
  // source VFO, read the frequency, select the target, write it, restore the
  // original selection. That needs a compound operation on the poller thread,
  // not a queued one-liner. A 404 is honest; a route that silently retuned the
  // wrong VFO would not be.

  for (const auto& route : generated) {
    server.Get(route.path, [h = route.handler, trusted](const HttpRequest&, HttpResponse& res) {
      WriteJson(res, 200, h(trusted));
    });
  }

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
