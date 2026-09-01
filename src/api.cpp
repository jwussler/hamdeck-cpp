#include "api.h"
#include "log.h"
#include <optional>
#include <filesystem>
#include <array>
#include <future>

#include <fstream>

#include <chrono>
#include <format>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <tuple>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <thread>

#include "alsa_devices.h"
#include "rig_cal.h"
#include "version.h"

namespace {

// The only routes an unauthenticated caller ever gets, whatever the settings:
// enough to learn the service is alive and to log in. Everything else -
// including the frequency, the meters and the receiver audio - is somebody's
// operating activity. Mirrors AlwaysAnonymousRoutes in the C# ApiServer.
namespace {

// A short, stable fingerprint of the binary that is answering. Not a build
// timestamp: two builds of identical source must compare equal, or a deploy
// check that says "changed" every time teaches people to ignore it.
std::string SelfBuildId() {
  std::ifstream f("/proc/self/exe", std::ios::binary);
  if (!f) return "unknown";
  // FNV-1a over the file. Cheap, no crypto dependency, and only ever compared
  // against itself - this is a change detector, not a signature.
  uint64_t h = 1469598103934665603ULL;
  char buf[64 * 1024];
  while (f.read(buf, sizeof(buf)) || f.gcount()) {
    const auto n = f.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
      h ^= static_cast<unsigned char>(buf[i]);
      h *= 1099511628211ULL;
    }
  }
  char out[17];
  std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
  return std::string(out).substr(0, 12);
}

}  // namespace

const std::set<std::string> kAlwaysAnonymous = {
    // ⚠️ /api/build reveals a hash of the binary and nothing else - no rig
    // state, no config, no user. It is anonymous because a deploy has to be
    // able to prove which binary answered BEFORE it has a session, and a
    // deploy check that needs a credential is a deploy check that gets skipped.
    "/api/health", "/api/auth/status", "/api/build",
};

// Read rig state without changing it. Anonymous ONLY when allow_anonymous_status
// is set. Mirrors ReadOnlyRoutes in the C# ApiServer.
// ⚠️ THE SOFTWARE VFO LOCK BLOCKS THESE. It is not a UI hint - an operator who
// has locked the VFO has said "do not move my frequency", and the host must
// enforce that for every client, including one that has never heard of the lock.
// Copied from the reference host's VfoLockExactBlocked / VfoLockPrefixBlocked.
const std::set<std::string> kVfoLockBlockedExact = {
    "/api/freq/send", "/api/freq/clear", "/api/freq/backspace",
    "/api/vfo/swap", "/api/quick-split",
};
const std::vector<std::string> kVfoLockBlockedPrefix = {
    "/api/freq/set/", "/api/freq/digit/", "/api/band/", "/api/preset/", "/api/step/",
};

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

std::string FreqBuffer(const ApiDeps& deps) {
  if (!deps.host) return "";
  std::lock_guard<std::mutex> lock(deps.host->mu);
  return deps.host->freq_buffer;
}

std::string StatusJson(const ApiDeps& deps) {
  if (!deps.poller) return R"({"connected":false,"stale":true})";
  const RigSnapshot s = deps.poller->Snapshot();
  bool host_vfo_locked = false, host_diversity = false;
  if (deps.host) {
    std::lock_guard<std::mutex> lock(deps.host->mu);
    host_vfo_locked = deps.host->vfo_locked;
    host_diversity = deps.host->diversity;
  }
  const long long age = deps.poller->CacheAgeMs();
  const bool stale = (age < 0) || (age > RadioPoller::kStaleAfterMs);
  return std::format(
      R"({{"connected":{},"freq":{},"mode":"{}","vfo":"{}","power":{},"tx":{},)"
      R"("tx_timeout_in":{},"split":{},"amp_tuning":{},"tgxl_tuning":{},)"
      R"("freq_buffer":"{}","vfo_locked":{},"diversity":{},"cache_age_ms":{},"stale":{}}})",
      JsonBool(s.connected), s.freq, s.mode, s.vfo, s.power, JsonBool(s.tx),
      deps.poller->TransmitSecondsRemaining(),
      // ⚠️ REPORTED LIVE, not hardcoded false. These were literals, so a tune in
      // progress was invisible to every client: the panel could not show the
      // carrier it had started, and could not offer the second press that stops
      // it. The reference host reports both from the tuners themselves.
      JsonBool(s.split), JsonBool(deps.amp && deps.amp->IsActive()),
      JsonBool(deps.tgxl && deps.tgxl->IsActive()),
      FreqBuffer(deps), JsonBool(host_vfo_locked), JsonBool(host_diversity),
      age < 0 ? 0 : age, JsonBool(stale));
}

// Field order matches the reference host exactly. Order is not required by JSON,
// but a diff of two captures is far easier to read when it does.
std::string StatusFullJson(const ApiDeps& deps) {
  if (!deps.poller) return "{}";
  const RigSnapshot s = deps.poller->Snapshot();
  return std::format(
      R"({{"ant":{},"rxant":{},"nb":{},"nr":{},"notch":{},"lock":{},"preamp":{},)"
      R"("att":{},"agc":"{}","vox":{},"comp":{},"mon":{},"rit":{},"rit_offset":{},)"
      R"("xit":{},"freq_b":{},"rf_gain":{},"rxant_km":0,)"
      R"("af_gain":{},"sub_af_gain":{},"cw_speed":{},"width_idx":{}}})",
      s.ant, JsonBool(s.rxant), JsonBool(s.nb), JsonBool(s.nr), JsonBool(s.notch),
      JsonBool(s.vfo_locked), s.preamp, JsonBool(s.att), s.agc, JsonBool(s.vox),
      JsonBool(s.comp), JsonBool(s.mon), JsonBool(s.rit), s.rit_offset,
      JsonBool(s.xit), s.freq_b, s.rf_gain,
      // Additive: the reference host does not report these, but a panel with an
      // AF-gain knob needs to know where the knob is.
      s.af_gain, s.sub_af_gain, s.cw_speed, s.width_idx);
}

std::string MetersJson(const ApiDeps& deps) {
  if (!deps.poller) return R"({"status":"ok","s_meter":0,"swr":0,"alc":0,"power":0})";
  const RigSnapshot s = deps.poller->Snapshot();
  // s_meter stays the raw 0-255 the reference host reports, so existing clients
  // are unaffected. s_meter_db and s_unit are ADDITIVE: the calibration is
  // rig-specific knowledge and belongs with the rig, not copy-pasted into every
  // client that wants to show a real scale.
  const int db = RawToDb(s.s_meter);
  // The raw four stay exactly as the reference host reports them. Everything
  // after is ADDITIVE and carries the unit in its name, so no client can mistake
  // a percentage for watts.
  return std::format(
      R"({{"status":"ok","s_meter":{},"swr":{},"alc":{},"power":{},)"
      R"("s_meter_db":{},"s_unit":"{}","swr_ratio":{:.1f},)"
      R"("alc_pct":{},"power_pct":{}}})",
      s.s_meter, s.swr, s.alc, s.power_mtr, db, DbToSUnit(db),
      SwrFromRaw(s.swr), AlcPercentFromRaw(s.alc), PowerPercentFromRaw(s.power_mtr));
}

std::string HealthJson(const ApiDeps& deps, int bound_port) {
  const bool connected = deps.poller && deps.poller->Snapshot().connected;
  return std::format(
      R"({{"status":"ok","service":"{}","version":"{}","port":{},)"
      R"("rig_connected":{},"amp_tuning":{},"tgxl_tuning":{},"freq_buffer":"{}"}})",
      kServiceName, kVersion, bound_port, JsonBool(connected),
      JsonBool(deps.amp && deps.amp->IsActive()),
      JsonBool(deps.tgxl && deps.tgxl->IsActive()), FreqBuffer(deps));
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

// ModeCode() now lives in radio.h/.cpp - the tuner needs it too.

// Band plan, copied from the reference host's BandHelper - not inferred. The
// "below 10 MHz is LSB" rule alone would put 60m and 30m on the wrong mode.
std::string ModeForFrequency(long long hz) {
  if (hz >= 5300000 && hz <= 5500000) return "USB";    // 60m, USB per band plan
  if (hz >= 10100000 && hz <= 10150000) return "CW";   // 30m, CW/digital only
  return hz < 10000000 ? "LSB" : "USB";
}

std::string Pad(long long v, int width) {
  std::string s = std::to_string(v);
  return std::string(width > (int)s.size() ? width - s.size() : 0, '0') + s;
}

// ⚠️ POWER CAP, PORTED FAITHFULLY AND DELIBERATELY NOT "FIXED".
// In the C# host a LOCAL caller is capped at 100 W while a remote caller gets
// 200 W - which reads backwards, so it is exactly the kind of thing to port
// as-is and ask about rather than quietly invert. Flagged in WIP.md.
// Hard ceiling on how long unkeying may be delayed, whatever the buffer says.
constexpr int kMaxDrainMs = 1200;

constexpr int kLocalPowerCap = 100;
// ⚠️ How recently another session must have touched the host to count as "in
// use". The Qt client polls status about once a second, so 15s survives a
// stalled poll or a brief network hiccup without declaring the operator gone,
// and still lets a closed client release the station inside a quarter minute.
//
// This is a WINDOW, not a login check. A session lives for hours; being logged
// in is not the same as sitting at the radio, and the whole point of this route
// is telling those two apart.
constexpr int kRemoteActiveWindowSeconds = 15;
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

  // ── The admin gate ─────────────────────────────────────────────────────────
  // ⚠️ /api/admin/* needs ADMIN, not merely a session. These routes add users,
  // change passwords, revoke transmit rights and end other people's sessions.
  //
  // It runs in the same place as the auth gate, before routing, so an admin
  // route added later is covered without anyone remembering - and it is checked
  // on the LOCAL listener too. Local callers skip authentication because the
  // kernel vouches for where they came from, but "is this an admin" is a
  // question about a user, and there is no user on an unauthenticated port.
  server.SetAdminGate([&deps](const HttpRequest& req, HttpResponse& res) -> bool {
    if (req.path.rfind("/api/admin/", 0) != 0) return true;
    const std::string token = ExtractToken(req);
    if (deps.auth && deps.auth->IsAdmin(token)) return true;
    WriteJson(res, 403, R"({"status":"error","message":"Admin access required"})");
    return false;
  });

  // ── The VFO lock gate ──────────────────────────────────────────────────────
  // Runs on every path, like the auth gate, so a frequency-moving route added
  // later is covered without anyone remembering to check. Enforced on BOTH
  // listeners: a local caller is trusted for auth, but the lock is the
  // operator's instruction about their own radio, not a permission level.
  server.SetSecondGate([&deps](const HttpRequest& req, HttpResponse& res) -> bool {
    if (!deps.host) return true;
    bool locked = false;
    {
      std::lock_guard<std::mutex> lock(deps.host->mu);
      locked = deps.host->vfo_locked;
    }
    if (!locked) return true;
    bool blocked = kVfoLockBlockedExact.count(req.path) > 0;
    for (const auto& p : kVfoLockBlockedPrefix) {
      if (req.path.rfind(p, 0) == 0) blocked = true;
    }
    if (!blocked) return true;
    WriteJson(res, 200,
              R"({"status":"error","message":"VFO is locked","vfo_locked":true})");
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
  // ── Capability reporting ───────────────────────────────────────────────────
  // ⚠️ CARRYOVER.md section 1: on the reference Linux build /api/record/start
  // answers {"status":"ok","recording":true} while Start() sets IsRecording =
  // false. A 200 there means the route exists, not that anything is recording.
  //
  // The rule taken from that: if a capability is absent, its STATUS route says
  // so. Never a cheerful 200 that only proves the route was registered.
  // Field names and types copied from the reference host so the client's probe
  // reads the same shape. The VALUES are honest: nothing is recording and there
  // is no capture backend, which is what available:false says.
  //
  // ⚠️ CARRYOVER.md section 1: the reference /api/record/start answers
  // {"status":"ok","recording":true} while Start() sets IsRecording = false. The
  // only honest signal is this route's file_recording. Do not reproduce the lie.
  // ⚠️ Every field here is derived from what ACTUALLY happened, never from
  // having been asked. CARRYOVER.md section 1: the reference /api/record/start
  // answers ok/recording:true while Start() sets IsRecording = false.
  {
    Recorder* rec = deps.recorder;
    server.Get("/api/record/status", [rec, &deps](const HttpRequest&, HttpResponse& res) {
      const bool on = rec && rec->recording();
      WriteJson(res, 200,
                std::format(
                    R"({{"recording":{},"buffering":{},"available":{},)"
                    R"("backend":"{}","device":"{}","sample_rate":{},)"
                    R"("frames_fed":{},"file_recording":{},"replay":false,)"
                    R"("recorded_seconds":{},"reason":"{}"}})",
                    JsonBool(on), JsonBool(rec && rec->buffering()),
                    JsonBool(rec && rec->available()),
                    rec && rec->available() ? "alsa/libasound" : "none",
                    deps.config ? deps.config->alsa_capture_device : "",
                    deps.config ? deps.config->record_sample_rate : 0,
                    rec ? rec->frames_fed() : 0,
                    JsonBool(on),   // the only honest signal, and it matches
                    rec ? rec->recorded_seconds() : 0,
                    rec && rec->available() ? "" : (rec ? rec->unavailable_reason()
                                                        : "no recorder")));
    });

    auto report = [](HttpResponse& res, const Recorder::Result& r, const char* action) {
      WriteJson(res, r.ok ? 200 : 409,
                std::format(R"({{"status":"{}","action":"{}","filename":"{}",)"
                            R"("message":"{}"}})",
                            r.ok ? "ok" : "error", action, r.filename, r.message));
    };
    server.Get("/api/record/start", [rec, report](const HttpRequest&, HttpResponse& res) {
      if (!rec) { WriteJson(res, 409, R"({"status":"error","message":"no recorder"})"); return; }
      report(res, rec->Start(), "started");
    });
    SessionStats* st = deps.stats;
    server.Get("/api/record/stop", [rec, report, st](const HttpRequest&, HttpResponse& res) {
      if (!rec) { WriteJson(res, 409, R"({"status":"error","message":"no recorder"})"); return; }
      auto r = rec->Stop();
      if (r.ok && st) st->CountRecording();
      report(res, r, "stopped");
    });
    server.Get("/api/record/toggle", [rec, report](const HttpRequest&, HttpResponse& res) {
      if (!rec) { WriteJson(res, 409, R"({"status":"error","message":"no recorder"})"); return; }
      if (rec->recording()) report(res, rec->Stop(), "stopped");
      else report(res, rec->Start(), "started");
    });
    server.Get("/api/record/toggle/stereo", [rec, report](const HttpRequest&, HttpResponse& res) {
      // The rig's receive audio is MONO. The reference has a stereo variant for
      // a two-receiver capture this host does not do, so it is the same call
      // rather than a silent pretence at a second channel.
      if (!rec) { WriteJson(res, 409, R"({"status":"error","message":"no recorder"})"); return; }
      if (rec->recording()) report(res, rec->Stop(), "stopped");
      else report(res, rec->Start(), "started");
    });
    server.Get("/api/record/replay", [rec, report](const HttpRequest&, HttpResponse& res) {
      if (!rec) { WriteJson(res, 409, R"({"status":"error","message":"no recorder"})"); return; }
      report(res, rec->SaveReplay(), "replay");
    });
  }

  // Session stats. Counted on the HOST from what the rig reports, so every
  // client sees the same numbers and closing a window does not reset them.
  {
    SessionStats* st = deps.stats;
    auto emit = [](HttpResponse& res, const SessionStats::Snapshot& s) {
      std::string bands, modes;
      for (const auto& [k, v] : s.band_changes)
        bands += std::format(R"({}"{}":{})", bands.empty() ? "" : ",", k, v);
      for (const auto& [k, v] : s.mode_changes)
        modes += std::format(R"({}"{}":{})", modes.empty() ? "" : ",", k, v);
      WriteJson(res, 200,
                std::format(
                    R"({{"status":"ok","session_duration":"{}","session_seconds":{},)"
                    R"("qsy_count":{},"tx_count":{},"tx_time":"{}","tx_seconds":{},)"
                    R"("qso_count":{},"recordings":{},)"
                    R"("band_changes":{{{}}},"mode_changes":{{{}}}}})",
                    SessionStats::Hms(s.session_seconds), s.session_seconds,
                    s.qsy_count, s.tx_count, SessionStats::Hms(s.tx_seconds),
                    s.tx_seconds,
                    // ⚠️ qso_count is kept for the reference clients, and it
                    // means what it meant there: finished recordings. It is not
                    // a logbook and is not presented as one.
                    s.recordings, s.recordings, bands, modes));
    };
    server.Get("/api/session", [st, emit](const HttpRequest&, HttpResponse& res) {
      if (!st) { WriteJson(res, 503, R"({"status":"error","message":"no session stats"})"); return; }
      emit(res, st->Get());
    });
    server.Get("/api/session/reset", [st, emit](const HttpRequest&, HttpResponse& res) {
      if (!st) { WriteJson(res, 503, R"({"status":"error","message":"no session stats"})"); return; }
      st->Reset();
      emit(res, st->Get());
    });
  }

  // Voice keyer: present on the reference host and answering, contrary to the
  // note in CARRYOVER.md section 1 that lists it among the null services.
  server.Get("/api/voice/status", [](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, R"({"status":"ok","playing":false})");
  });
  server.Get("/api/voice/stop", [](const HttpRequest&, HttpResponse& res) {
    WriteJson(res, 200, R"({"status":"ok","voice":"stopped"})");
  });

  server.Get("/api/tx-audio/devices", [](const HttpRequest&, HttpResponse& res) {
    std::string devices;
    for (const auto& d : ListPlaybackDevices()) {
      if (!devices.empty()) devices += ",";
      devices += std::format(R"({{"index":{},"name":"{}"}})", d.index, d.name);
    }
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","devices":[{}],"current":-1}})", devices));
  });

  server.Get("/api/tx-audio/status", [&deps](const HttpRequest&, HttpResponse& res) {
    // ⚠️ available reflects whether audio can actually REACH THE RIG, not
    // whether the route exists. With the null sink the framing works end to end
    // and nothing is transmitted, so available stays false and says why.
    TxAudioReceiver* tx = deps.tx_audio;
    const bool real_sink = tx && tx->Backend().find("null sink") == std::string::npos;
    // Exactly the reference host's four fields. Diagnostics (backend, accepted,
    // dropped, queue depth) live on /api/backend, which is ours alone - adding
    // them here would make the client's probe see a shape it does not know.
    WriteJson(res, 200,
              std::format(
                  R"({{"status":"ok","available":{},"active":{},"client_connected":{}}})",
                  JsonBool(real_sink),
                  JsonBool(tx && !tx->Holder().empty()),
                  JsonBool(tx && !tx->Holder().empty())));
  });

  // ⚠️ IS SOMEBODY ELSE OPERATING THE STATION RIGHT NOW?
  //
  // This exists because the flag everything reaches for first is the wrong one.
  // `client_connected` on /api/tx-audio/status above means SOMEONE IS HOLDING
  // THE TX AUDIO - it is the transmit holder under another name. An operator
  // listening remotely on RX, spinning the VFO, working nobody, reads false
  // there. A helper that stood down on that flag would fight the very client it
  // was supposed to yield to, and only while that client was receiving.
  //
  // Two independent signals, because they fail in different directions:
  //   - a recently active session that is NOT the caller's (covers RX-only use)
  //   - the TX audio holder (covers a long transmission, where the session's
  //     last_activity can go quiet for the length of the over)
  server.Get("/api/remote/status", [&deps](const HttpRequest& req, HttpResponse& res) {
    AuthService* auth = deps.auth;
    TxAudioReceiver* tx = deps.tx_audio;
    const std::string holder = tx ? tx->Holder() : std::string();
    // ⚠️ Excluding the CALLER's own session is what makes this answerable. See
    // AuthService::ActiveSessionsExcluding - a poller refreshes itself on the
    // way in and would otherwise always find somebody home.
    const AuthService::ActiveCount n =
        auth ? auth->ActiveSessionsExcluding(ExtractToken(req),
                                             kRemoteActiveWindowSeconds)
             : AuthService::ActiveCount{};
    const bool active = (n.others > 0) || !holder.empty();
    // ⚠️ same_user_clients is reported so a caller can recognise ITS OWN GHOST.
    // A helper that restarts leaves its previous session alive until it ages out;
    // that session is not the caller's token, so it counts as somebody else and
    // the helper stands down against itself - permanently, if it is restarting in
    // a loop. Measured: a second run within the window read active=true with
    // nothing but the previous run on the host.
    //
    // A COUNT, not a username: the caller only needs to know how many of these
    // are its own, and a session list is not something a non-admin route should
    // hand out.
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","active":{},"other_clients":{},)"
                          R"("same_user_clients":{},"tx_holder":"{}",)"
                          R"("window_seconds":{}}})",
                          JsonBool(active), n.others, n.same_user, holder,
                          kRemoteActiveWindowSeconds));
  });

  // The meter scale, so a client draws a calibrated face without hard-coding a
  // table for a radio it may not be talking to. Swap the rig and every client's
  // scale follows without shipping a new client.
  server.Get("/api/meters/scale", [](const HttpRequest&, HttpResponse& res) {
    std::string points, ticks;
    for (const auto& p : SMeterCalibration()) {
      if (!points.empty()) points += ",";
      points += std::format(R"({{"raw":{},"db":{}}})", p.raw, p.db);
    }
    for (const auto& t : SMeterScaleTicks()) {
      if (!ticks.empty()) ticks += ",";
      ticks += std::format(R"({{"raw":{},"label":"{}"}})", t.raw, t.label);
    }
    WriteJson(res, 200,
              std::format(
                  R"({{"status":"ok","raw_max":255,"s9_raw":160,)"
                  R"("source":"hamlib FTDX101D_STR_CAL - not measured on this station",)"
                  R"("calibration":[{}],"ticks":[{}],)"
                  R"("swr":{{"unit":"ratio","source":"hamlib yaesu_default_swr_cal, )"
                  R"(tested on an FT-991 not this rig","warn_above":2.0}},)"
                  R"("alc":{{"unit":"percent","source":"hamlib yaesu_default_alc_cal - )"
                  R"(full scale is raw 64, not 255"}},)"
                  R"("power":{{"unit":"percent","source":"hamlib )"
                  R"(yaesu_default_rfpower_meter_cal, read as percent of rated output. )"
                  R"(NOT watts: that table is for a 100 W radio and this is a 200 W rig"}}}})",
                  points, ticks));
  });

  // The running binary's own identity, so a deploy can prove the service came
  // back on the binary that was just installed. Computed ONCE at startup from
  // /proc/self/exe: reading it per request would hash a multi-megabyte file on
  // the same box that is servicing a transmitter.
  server.Get("/api/build", [](const HttpRequest&, HttpResponse& res) {
    static const std::string id = SelfBuildId();
    WriteJson(res, 200, std::format(R"({{"status":"ok","build":"{}","version":"{}"}})",
                                    id, HAMDECK_VERSION));
  });

  // ── Per-user settings profile ──────────────────────────────────────────────
  // One JSON file per user, so an operator's preferences follow them to any
  // machine instead of living in one PC's registry. Mic gain is the reason this
  // exists: it reverted to 100% on every restart, which pins the rig's ALC.
  //
  // ⚠️ THE BODY IS OPAQUE TO THE HOST. It stores and returns whatever the client
  // sends. That means it must never be trusted as configuration here, and the
  // client must never put a credential in it - it is handed back to anything
  // that can log in as that user.
  {
    AuthService* prof_auth = deps.auth;
    const std::string dir = deps.profile_dir;

    // ⚠️ THE USERNAME BECOMES A FILENAME. Without this a username containing
    // ".." or "/" would read and write anywhere the service user can reach. Only
    // a conservative character set is allowed, and anything else is refused
    // outright rather than sanitised into something surprising.
    auto safe_name = [](const std::string& user) -> std::optional<std::string> {
      if (user.empty() || user.size() > 64) return std::nullopt;
      for (const char c : user) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return std::nullopt;
      }
      if (user.front() == '.') return std::nullopt;   // no dotfiles, no ".."
      return user;
    };

    auto who_for = [prof_auth, trusted](const HttpRequest& req) -> std::string {
      const std::string token = ExtractToken(req);
      const auto user = prof_auth ? prof_auth->Username(token) : std::nullopt;
      if (user && !user->empty()) return *user;
      // The control listener is loopback-only and already trusted for auth.
      return trusted ? std::string("local") : std::string();
    };

    server.Get("/api/profile", [dir, who_for, safe_name](const HttpRequest& req,
                                                         HttpResponse& res) {
      if (dir.empty()) {
        WriteJson(res, 200, R"({"status":"ok","stored":false,"profile":{},)"
                            R"("message":"no profile directory configured"})");
        return;
      }
      const auto name = safe_name(who_for(req));
      if (!name) {
        WriteJson(res, 403, R"({"status":"error","message":"no user"})");
        return;
      }
      std::ifstream in(dir + "/" + *name + ".json");
      if (!in) {
        // ⚠️ Absent is not an error, and it is not an empty profile silently
        // presented as a saved one. The client needs to know it has nothing
        // stored so it keeps its local settings instead of wiping them.
        WriteJson(res, 200, R"({"status":"ok","stored":false,"profile":{}})");
        return;
      }
      std::string body((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","stored":true,"profile":{}}})",
                            body.empty() ? "{}" : body));
    });

    server.Post("/api/profile", [dir, who_for, safe_name](const HttpRequest& req,
                                                          HttpResponse& res) {
      if (dir.empty()) {
        WriteJson(res, 503, R"({"status":"error","message":"no profile directory"})");
        return;
      }
      const auto name = safe_name(who_for(req));
      if (!name) {
        WriteJson(res, 403, R"({"status":"error","message":"no user"})");
        return;
      }
      // ⚠️ A cap, because this writes to the station's disk on an authenticated
      // request. 64 KB is far more than a settings blob and far less than a
      // problem.
      if (req.body.size() > 64 * 1024) {
        WriteJson(res, 413, R"({"status":"error","message":"profile too large"})");
        return;
      }
      if (req.body.empty() || req.body.front() != '{') {
        WriteJson(res, 400, R"({"status":"error","message":"expected a JSON object"})");
        return;
      }
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);

      // ⚠️ Temp file then rename, so an interrupted write cannot leave a
      // half-written profile that then fails to parse on the next login. The
      // same rule the config writer and set_password.py follow.
      const std::string final_path = dir + "/" + *name + ".json";
      const std::string tmp_path = final_path + ".tmp";
      {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) {
          WriteJson(res, 500, R"({"status":"error","message":"cannot write profile"})");
          return;
        }
        out << req.body;
        out.flush();
        if (!out) {
          WriteJson(res, 500, R"({"status":"error","message":"write failed"})");
          return;
        }
      }
      std::filesystem::rename(tmp_path, final_path, ec);
      if (ec) {
        WriteJson(res, 500, R"({"status":"error","message":"could not replace profile"})");
        return;
      }
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","stored":true,"bytes":{}}})",
                            req.body.size()));
    });
  }

  server.Get("/api/backend", [&deps](const HttpRequest&, HttpResponse& res) {
    TxAudioReceiver* tx = deps.tx_audio;
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","cat":"{}","simulated":{},)"
                          R"("rx_audio":"{}","tx_audio":"{}",)"
                          R"("tx_accepted":{},"tx_dropped":{},"tx_queue":{},)"
                          R"("tx_peak":{},"device_queued_ms":{}}})",
                          deps.poller ? deps.poller->Backend() : "none",
                          JsonBool(deps.simulated),
                          deps.rx_audio ? deps.rx_audio->Backend() : "none",
                          tx ? tx->Backend() : "none",
                          tx ? tx->Accepted() : 0, tx ? tx->Dropped() : 0,
                          tx ? tx->QueueDepth() : 0,
                          tx ? tx->PeakSinceReset() : 0,
                          deps.queued_audio_ms ? deps.queued_audio_ms() : -1));
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

      // ⚠️ PTT ON is here; PTT OFF is below, because unkeying is not instant. Unkeying must wait for the audio still
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
         const int v = rig ? rig->Snapshot().rf_gain : 0;
         return std::format(R"({{"status":"ok","rf_gain":{},"raw":{}}})", v * 100 / 255, v); }},

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
      // NOTE: comp is NOT here - PR0 takes 1=OFF / 2=ON, not 0/1. Handled below.
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

  // ⚠️ Compressor: PR0P2 where P2 is 1=OFF and 2=ON - NOT a 0/1 flag. Sending
  // PR01 for "on" would have turned it OFF, and PR00 for "off" is not a valid
  // value at all. Found by reading the real radio, which answered PR01; with the
  // compressor off.
  {
    generated.push_back({"/api/comp/on", [rig](bool) {
        if (rig) rig->Enqueue("PR02;");
        return OkJson("comp", "1"); }});
    generated.push_back({"/api/comp/off", [rig](bool) {
        if (rig) rig->Enqueue("PR01;");
        return OkJson("comp", "0"); }});
    generated.push_back({"/api/comp/toggle", [rig](bool) {
        const bool now = rig && rig->Snapshot().comp;
        if (rig) rig->Enqueue(now ? "PR01;" : "PR02;");
        return OkJson("comp", now ? "false" : "true"); }});
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
  // ── Unkeying, which is NOT the reverse of keying ──────────────────────────
  //
  // ⚠️ WAIT FOR THE AUDIO THAT IS ALREADY ON THE DEVICE, THEN DROP PTT.
  // The last fraction of a second of every transmission is sitting in the ALSA
  // buffer when the operator releases PTT. Drop the carrier first and that audio
  // is never transmitted - the end of every over is cut off, and it sounds like
  // the other station stopped listening. CARRYOVER.md section 4a; the bug it
  // describes took a report from a net to find.
  //
  // ⚠️ WAIT THE DEPTH AT THIS MOMENT, NOT "UNTIL EMPTY". The microphone stays
  // open, so an until-empty loop never terminates and the carrier stays up.
  // Snapshot the depth, wait exactly that long, unkey.
  //
  // ⚠️ HARD CAP. A mis-measured or stuck buffer must not hold the transmitter
  // open. Nothing is worth an open carrier.
  auto unkey = [rig, &deps](bool) {
    int drained = 0;
    if (deps.queued_audio_ms) {
      const int q = deps.queued_audio_ms();
      if (q > 0) drained = q > kMaxDrainMs ? kMaxDrainMs : q;
    }
    if (rig) {
      // Runs on the poller thread, which owns the serial port. The wait happens
      // there so the unkey is not racing a status poll for the port.
      rig->EnqueueTask([drained](CatTransport& cat) {
        if (drained > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(drained));
        }
        cat.Send("TX0;");
      });
    }
    return std::format(R"({{"status":"ok","ptt":0,"drained_ms":{}}})", drained);
  };
  generated.push_back({"/api/ptt/off",   unkey});
  generated.push_back({"/api/ptt/unkey", unkey});
  generated.push_back({"/api/ptt/toggle", [rig, unkey](bool is_local) {
      const bool keyed = rig && rig->Snapshot().tx;
      if (keyed) return unkey(is_local);
      if (rig) rig->Enqueue("TX1;");
      return std::string(R"({"status":"ok","ptt":true})"); }});

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

  // ── Volume / mute ──────────────────────────────────────────────────────────
  // AF gain is AG0<nnn> (main) and AG1<nnn> (sub), 0-255.
  HostState* host = deps.host;
  // ⚠️ Shapes copied from the reference host, not invented: volume and rf-gain
  // report a PERCENTAGE plus the raw 0-255 value, and up/down answer a bare
  // {"status":"ok"} with no level in it. A first pass here guessed a raw-only
  // reply and a 16-step; both were wrong, and a client reading `volume` would
  // have shown 0-255 in a 0-100 control.
  generated.push_back({"/api/volume/get", [rig](bool) {
      const int v = rig ? rig->Snapshot().af_gain : 0;
      return std::format(R"({{"status":"ok","volume":{},"raw":{}}})", v * 100 / 255, v); }});
  auto volume_step = [rig](int delta) {
    return [rig, delta](bool) {
      int v = (rig ? rig->Snapshot().af_gain : 0) + delta;
      v = v < 0 ? 0 : (v > 255 ? 255 : v);
      if (rig) rig->Enqueue(std::format("AG0{:03d};", v));
      return std::string(R"({"status":"ok"})");
    };
  };
  generated.push_back({"/api/volume/up",   volume_step(13)});   // 13, per the reference
  generated.push_back({"/api/volume/down", volume_step(-13)});

  // ⚠️ Mute REMEMBERS the level so unmute restores it. Unmuting to a fixed
  // default would blast the operator or leave them wondering why it is quiet.
  auto mute_group = [rig, host](const char* key, bool main_band, bool sub_band) {
    struct Ops { std::function<std::string(bool)> on, off, toggle; };
    auto set = [rig, host, main_band, sub_band](bool mute) {
      if (!rig || !host) return;
      std::lock_guard<std::mutex> lock(host->mu);
      const RigSnapshot s = rig->Snapshot();
      if (mute) {
        if (main_band && s.af_gain > 0)     host->pre_mute_af = s.af_gain;
        if (sub_band  && s.sub_af_gain > 0) host->pre_mute_sub_af = s.sub_af_gain;
        if (main_band) rig->Enqueue("AG0000;");
        if (sub_band)  rig->Enqueue("AG1000;");
      } else {
        if (main_band) rig->Enqueue(std::format("AG0{:03d};", host->pre_mute_af));
        if (sub_band)  rig->Enqueue(std::format("AG1{:03d};", host->pre_mute_sub_af));
      }
    };
    const std::string k = key;
    return Ops{
        [set, k](bool) { set(true);  return OkJson(k, "1"); },
        [set, k](bool) { set(false); return OkJson(k, "0"); },
        [set, k, rig, main_band, sub_band](bool) {
          const RigSnapshot s = rig ? rig->Snapshot() : RigSnapshot{};
          const bool muted = (main_band ? s.af_gain == 0 : true) &&
                             (sub_band  ? s.sub_af_gain == 0 : true);
          set(!muted);
          return OkJson(k, muted ? "false" : "true");
        }};
  };
  for (const auto& [path, key, m, sub] :
       std::vector<std::tuple<const char*, const char*, bool, bool>>{
           {"/api/mute", "mute", true, false},
           {"/api/mute-sub", "mute_sub", false, true},
           {"/api/mute-all", "mute_all", true, true}}) {
    auto ops = mute_group(key, m, sub);
    generated.push_back({strdup((std::string(path) + "/on").c_str()), ops.on});
    generated.push_back({strdup((std::string(path) + "/off").c_str()), ops.off});
    generated.push_back({strdup((std::string(path) + "/toggle").c_str()), ops.toggle});
  }

  // ── Frequency entry buffer (host state, not the rig) ───────────────────────
  generated.push_back({"/api/freq/get", [host](bool) {
      if (!host) return std::string(R"({"status":"ok","buffer":"","length":0})");
      std::lock_guard<std::mutex> lock(host->mu);
      return std::format(R"({{"status":"ok","buffer":"{}","length":{}}})",
                         host->freq_buffer, host->freq_buffer.size()); }});
  generated.push_back({"/api/freq/clear", [host](bool) {
      if (host) { std::lock_guard<std::mutex> lock(host->mu); host->freq_buffer.clear(); }
      return OkJson("buffer", Quoted("")); }});
  generated.push_back({"/api/freq/backspace", [host](bool) {
      std::string b;
      if (host) {
        std::lock_guard<std::mutex> lock(host->mu);
        if (!host->freq_buffer.empty()) host->freq_buffer.pop_back();
        b = host->freq_buffer;
      }
      return OkJson("buffer", Quoted(b)); }});

  // ── Software locks the rig does not hold ───────────────────────────────────
  auto host_flag = [host](const char* key, bool HostState::* field) {
    struct Ops { std::function<std::string(bool)> on, off, toggle, status; };
    const std::string k = key;
    return Ops{
        [host, field, k](bool) {
          if (host) { std::lock_guard<std::mutex> l(host->mu); (host->*field) = true; }
          return std::format(R"({{"status":"ok","{}":true}})", k); },
        [host, field, k](bool) {
          if (host) { std::lock_guard<std::mutex> l(host->mu); (host->*field) = false; }
          return std::format(R"({{"status":"ok","{}":false}})", k); },
        [host, field, k](bool) {
          bool v = false;
          if (host) { std::lock_guard<std::mutex> l(host->mu); (host->*field) = !(host->*field); v = (host->*field); }
          return std::format(R"({{"status":"ok","{}":{}}})", k, JsonBool(v)); },
        [host, field, k](bool) {
          bool v = false;
          if (host) { std::lock_guard<std::mutex> l(host->mu); v = (host->*field); }
          return std::format(R"({{"status":"ok","{}":{}}})", k, JsonBool(v)); }};
  };
  {
    auto vl = host_flag("vfo_locked", &HostState::vfo_locked);
    generated.push_back({"/api/vfo-lock/on", vl.on});
    generated.push_back({"/api/vfo-lock/off", vl.off});
    generated.push_back({"/api/vfo-lock/toggle", vl.toggle});
    generated.push_back({"/api/vfo-lock/status", vl.status});
    auto dv = host_flag("diversity", &HostState::diversity);
    generated.push_back({"/api/diversity/on", dv.on});
    generated.push_back({"/api/diversity/off", dv.off});
    generated.push_back({"/api/diversity/toggle", dv.toggle});
    generated.push_back({"/api/diversity/status", dv.status});
  }

  // ── Compound sequences: not one verb, so they run on the poller thread ─────
  generated.push_back({"/api/quick-split", [rig](bool) {
      if (rig) rig->EnqueueTask([](CatTransport& cat) {
        // Read A, put A+5kHz on B, come back to A, enable split. Done as one
        // task so a status poll cannot read a half-applied state.
        auto fa = cat.Exchange("FA;");
        if (!fa || fa->size() < 12) return;
        const long long f = std::stoll(fa->substr(2, 9)) + 5000;
        cat.Send("VS1;");
        cat.Send(std::format("FA{:09d};", f));
        cat.Send("VS0;");
        cat.Send("ST1;");
      });
      return OkJson("offset", "5000"); }});

  auto vfo_copy = [rig](bool a_to_b) {
    return [rig, a_to_b](bool) {
      if (rig) rig->EnqueueTask([a_to_b](CatTransport& cat) {
        const char* src = a_to_b ? "FA;" : "FB;";
        auto r = cat.Exchange(src);
        if (!r || r->size() < 12) return;
        const long long f = std::stoll(r->substr(2, 9));
        cat.Send((a_to_b ? "FB" : "FA") + std::format("{:09d};", f));
      });
      return OkJson("action", Quoted(a_to_b ? "a2b" : "b2a"));
    };
  };
  generated.push_back({"/api/vfo-copy/a2b", vfo_copy(true)});
  generated.push_back({"/api/vfo-copy/b2a", vfo_copy(false)});

  // ── Remote TX: menu items that route the mic to the USB codec ─────────────
  // EX010111 MOD SOURCE (0=MIC, 1=REAR), EX010112 REAR SELECT (0=DATA, 1=USB),
  // EX010113 RPORT GAIN. Enabling saves the old gain so disabling restores it.
  // ⚠️ 50 ms BETWEEN THE MENU WRITES, copied from the reference host's
  // EnableRemoteTx. Sent back to back the rig takes the first and ignores the
  // rest, which is silent: the route still answers ok, the radio still keys, and
  // it transmits NOTHING because the modulation source never moved off MIC.
  // That cost an evening with a perfect audio chain and a dead transmitter.
  // ⚠️ THESE ROUTES READ THE RADIO BACK BEFORE THEY CLAIM ANYTHING.
  //
  // They used to enqueue three writes and answer ok immediately, which cannot
  // know whether the rig accepted them - and that is exactly how the dead
  // transmitter hid: the route said ok, the status route beside it invented
  // agreeing values, and the radio was doing something else entirely. An
  // optimistic ok on a route that changes the transmitter is not a status, it is
  // a hope.
  //
  // Same shape as tools/deploy.sh, which does not trust that an install worked -
  // it compares the running build id against the binary it just wrote. The cost
  // is one extra round trip on an operation done a handful of times a day, and
  // it is nowhere near the audio or PTT path.
  auto verify_remote_tx = [](RadioPoller* rig, bool want_rear) -> std::string {
    if (!rig) return R"({"status":"error","message":"no radio"})";
    auto result = std::make_shared<std::promise<std::array<std::optional<std::string>, 2>>>();
    auto fut = result->get_future();
    rig->EnqueueTask([result, want_rear](CatTransport& cat) {
      // ⚠️ 50 ms BETWEEN THE WRITES (§8g). Sent back to back the rig takes the
      // first and ignores the rest, silently.
      if (want_rear) {
        cat.Send("EX0101111;");   // MOD SOURCE -> REAR
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cat.Send("EX0101121;");   // REAR SELECT -> USB
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cat.Send("EX010113050;"); // RPORT GAIN
      } else {
        cat.Send("EX0101110;");   // MOD SOURCE -> MIC
      }
      // Let the rig settle before asking what it did, in the SAME task so
      // nothing else on the port lands between the write and the read.
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      std::array<std::optional<std::string>, 2> r;
      r[0] = cat.Exchange("EX010111;");
      r[1] = cat.Exchange("EX010112;");
      result->set_value(r);
    });
    if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
      // ⚠️ Unverified is NOT the same as failed. The commands were sent; we do
      // not know what happened, and saying so is the honest answer.
      return R"({"status":"ok","verified":false,)"
             R"("message":"commands sent, but the radio did not answer a read-back"})";
    }
    const auto r = fut.get();
    auto flag = [](const std::optional<std::string>& v, bool& known) -> bool {
      if (!v || v->size() < 9) { known = false; return false; }
      known = true;
      return (*v)[8] == '1';
    };
    bool k1 = false, k2 = false;
    const bool rear = flag(r[0], k1);
    const bool usb = flag(r[1], k2);
    if (!k1 || !k2) {
      return R"({"status":"ok","verified":false,)"
             R"("message":"could not read the menu items back"})";
    }
    const bool as_asked = want_rear ? (rear && usb) : (!rear);
    return std::format(
        R"({{"status":"ok","remote_tx":{},"verified":{},)"
        R"("mod_source_rear":{},"rear_select_usb":{},"message":"{}"}})",
        JsonBool(rear && usb), JsonBool(as_asked), JsonBool(rear), JsonBool(usb),
        as_asked ? (want_rear ? "SSB MOD SOURCE=REAR, REAR SELECT=USB"
                              : "SSB MOD SOURCE=MIC")
                 : "THE RADIO DID NOT TAKE THE CHANGE");
  };

  generated.push_back({"/api/remote-tx/on", [rig, verify_remote_tx](bool) {
      return verify_remote_tx(rig, true); }});
  generated.push_back({"/api/remote-tx/off", [rig, verify_remote_tx](bool) {
      return verify_remote_tx(rig, false); }});
  generated.push_back({"/api/remote-tx/status", [rig](bool) {
      // ⚠️ THIS ROUTE USED TO INVENT ITS ANSWER. It reported the rig's TX flag as
      // mod_source_rear and hardcoded the other two - a confident wrong answer,
      // which is worse than no answer: it was believed, and it sent the search
      // for a dead transmitter to the wrong end of the chain.
      //
      // It now asks the radio. EX010111 MOD SOURCE (0=MIC, 1=REAR), EX010112
      // REAR SELECT (0=DATA, 1=USB), EX010113 RPORT GAIN (000-100); the reply is
      // the command echoed with the value at offset 8, exactly as the reference
      // host parses it. If the read fails, SAY SO - never fall back to a value.
      if (!rig) return std::string(R"({"status":"error","message":"no radio"})");
      auto result = std::make_shared<std::promise<std::array<std::optional<std::string>, 3>>>();
      auto fut = result->get_future();
      rig->EnqueueTask([result](CatTransport& cat) {
        std::array<std::optional<std::string>, 3> r;
        r[0] = cat.Exchange("EX010111;");
        r[1] = cat.Exchange("EX010112;");
        r[2] = cat.Exchange("EX010113;");
        result->set_value(r);
      });
      if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
        return std::string(R"({"status":"error","message":"radio did not answer"})");
      }
      const auto r = fut.get();
      auto flag = [](const std::optional<std::string>& v) -> std::string {
        if (!v || v->size() < 9) return "null";   // unknown, and says so
        return (*v)[8] == '1' ? "true" : "false";
      };
      std::string gain = "null";
      if (r[2] && r[2]->size() >= 11) {
        try { gain = std::to_string(std::stoi(r[2]->substr(8, 3))); } catch (...) {}
      }
      return std::format(R"({{"status":"ok","mod_source_rear":{},"rear_select_usb":{},)"
                         R"("rport_gain":{}}})",
                         flag(r[0]), flag(r[1]), gain); }});

  generated.push_back({"/api/ssb-out-level/get", [rig](bool) {
      (void)rig;
      return std::string(R"({"status":"ok","level":50})"); }});

  // ── Frequency entry: apply the buffer ──────────────────────────────────────
  // Buffer semantics copied from the reference host: <=3 digits is whole MHz,
  // otherwise the last three digits are kHz. The mode follows the band plan,
  // which is why typing 7200 lands on LSB and 14200 on USB.
  generated.push_back({"/api/freq/send", [rig, host](bool) {
      std::string buf;
      if (host) { std::lock_guard<std::mutex> l(host->mu); buf = host->freq_buffer; }
      if (buf.empty()) return std::string(R"({"status":"error","message":"Buffer is empty"})");
      long long hz = 0;
      try {
        if (buf.size() <= 3) {
          hz = std::stoll(buf) * 1000000LL;
        } else {
          hz = std::stoll(buf.substr(0, buf.size() - 3)) * 1000000LL +
               std::stoll(buf.substr(buf.size() - 3)) * 1000LL;
        }
      } catch (const std::exception&) {
        return std::string(R"({"status":"error","message":"Buffer is not a number"})");
      }
      const std::string mode = ModeForFrequency(hz);
      if (rig) {
        rig->Enqueue(std::format("MD0{};", ModeCode(mode)));
        rig->Enqueue(std::format("FA{:09d};", hz));
      }
      if (host) { std::lock_guard<std::mutex> l(host->mu); host->freq_buffer.clear(); }
      return std::format(R"({{"status":"ok","freq_hz":{},"mode":"{}","cleared":true}})",
                         hz, mode); }});

  // ── Tuners: THREE different things, and confusing them is expensive ────────
  // ⚠️ /api/tune is the RIG'S INTERNAL ATU (AC002;). CARRYOVER.md section 2 is
  // explicit that it is the WRONG tuner for this station; the right one is
  // /api/tune/tgxl. They are kept separate and each names itself in its reply so
  // a confirmation dialog cannot say "tuning" and leave the operator guessing
  // which box just keyed up.
  generated.push_back({"/api/tune", [rig](bool) {
      if (rig) rig->Enqueue("AC002;");
      return std::string(R"({"status":"ok","action":"tuning","tuner":"rig-internal-atu"})"); }});

  // TGXL: an external tuner reached over the network. Not configured here (no
  // host in the config), so it says so instead of pretending to tune.
  // ⚠️ This is the RIGHT tuner for this station; /api/tune above is the rig's
  // internal ATU and is the wrong one. Each names itself in its reply so a
  // confirmation cannot just say "tuning".
  TgxlTuner* tgxl = deps.tgxl;
  auto tgxl_tune = [tgxl](bool) {
    if (!tgxl || !tgxl->configured()) {
      return std::string(R"({"status":"error","available":false,"tuner":"tgxl",)"
                         R"("message":"TGXL is not configured - set tgxl_host in the config"})");
    }
    const auto r = tgxl->Tune();
    return std::format(
        R"({{"status":"{}","tuner":"tgxl","available":true,"tuning":{},)"
        R"("action":"{}","message":"{}"}})",
        r.ok ? "ok" : "error", JsonBool(r.tuning), r.action, r.message);
  };
  generated.push_back({"/api/tune/tgxl", tgxl_tune});
  generated.push_back({"/api/tgxl/tune", tgxl_tune});
  generated.push_back({"/api/tune/tgxl/status", [tgxl](bool) {
      return std::format(R"({{"status":"ok","tuning":{},"available":{}}})",
                         JsonBool(tgxl && tgxl->IsActive()),
                         JsonBool(tgxl && tgxl->configured())); }});

  // ⚠️ AMP TUNE REFUSES EVERY REMOTE CALLER. CARRYOVER.md section 2. The check is
  // the LISTENER the request arrived on - the control port is bound to loopback,
  // so "local" is a kernel guarantee, not a header a caller can set.
  AmpTuner* amp = deps.amp;
  auto amp_tune = [amp](bool is_local) {
    if (!is_local) {
      return std::string(R"({"status":"error",)"
                         R"("message":"Amp tune is only available when connected locally."})");
    }
    if (!amp) {
      return std::string(R"({"status":"error","available":false,"tuner":"amp",)"
                         R"("message":"Amp tuner is not configured on this host"})");
    }
    const auto r = amp->Tune();
    return std::format(
        R"({{"status":"{}","tuner":"amp","available":true,"tuning":{},)"
        R"("action":"{}","message":"{}"}})",
        r.ok ? "ok" : "error", JsonBool(r.tuning), r.action, r.message);
  };
  generated.push_back({"/api/tune/amp", amp_tune});
  generated.push_back({"/api/amp/tune", amp_tune});
  generated.push_back({"/api/tune/amp/status", [amp](bool) {
      return std::format(R"({{"status":"ok","tuning":{},"available":{}}})",
                         JsonBool(amp && amp->IsActive()), JsonBool(amp != nullptr)); }});

  // ── CW keyer: present on the reference Linux host, not ported yet ─────────
  generated.push_back({"/api/cw/status", [](bool) {
      return std::string(R"({"status":"ok","playing":false,"available":false,)"
                         R"("reason":"CW keyer is not implemented in the C++ host yet"})"); }});
  generated.push_back({"/api/cw/stop", [](bool) {
      return std::string(R"({"status":"ok","cw":"stopped","available":false})"); }});

  // ── Prefix routes ──────────────────────────────────────────────────────────
  auto bad_request = [](HttpResponse& res, const std::string& msg) {
    WriteJson(res, 400, std::format(R"({{"status":"error","message":"{}"}})", msg));
  };
  auto parse_int = [](const std::string& s, int& out) {
    if (s.empty() || s.find_first_not_of("-0123456789") != std::string::npos) return false;
    try { out = std::stoi(s); return true; } catch (const std::exception&) { return false; }
  };

  server.GetPrefix("/api/mode/", [rig](const std::string& m, const HttpRequest&,
                                       HttpResponse& res) {
    std::string up = m;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    const int code = ModeCode(up);
    if (code == 0) {
      WriteJson(res, 400, std::format(R"({{"status":"error","message":"unknown mode {}"}})", up));
      return;
    }
    if (rig) rig->Enqueue(std::format("MD0{};", code));
    WriteJson(res, 200, OkJson("mode", Quoted(up)));
  });

  // Band plan centres, copied from the reference host's BandFrequencies. Phone
  // centres, except 30m which is CW/digital only - which is why the mode comes
  // from the band plan rather than being assumed.
  server.GetPrefix("/api/band/", [rig](const std::string& band, const HttpRequest&,
                                       HttpResponse& res) {
    static const std::map<std::string, long long> kBands = {
        {"160", 1880000},  {"80", 3860000},   {"60", 5330500},  {"40", 7200000},
        {"30", 10130000},  {"20", 14200000},  {"17", 18130000}, {"15", 21300000},
        {"12", 24940000},  {"10", 28400000},  {"6", 50125000},
    };
    const auto it = kBands.find(band);
    if (it == kBands.end()) {
      WriteJson(res, 400, std::format(R"({{"status":"error","message":"unknown band {}"}})", band));
      return;
    }
    const std::string mode = ModeForFrequency(it->second);
    if (rig) {
      rig->Enqueue(std::format("MD0{};", ModeCode(mode)));
      rig->Enqueue(std::format("FA{:09d};", it->second));
    }
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","band":"{}","freq":{},"mode":"{}"}})",
                          band, it->second, mode));
  });

  server.GetPrefix("/api/freq/set/", [rig](const std::string& hz_s, const HttpRequest&,
                                           HttpResponse& res) {
    long long hz = 0;
    try { hz = std::stoll(hz_s); } catch (const std::exception&) {
      WriteJson(res, 400, R"({"status":"error","message":"frequency is not a number"})");
      return;
    }
    const std::string mode = ModeForFrequency(hz);
    if (rig) {
      rig->Enqueue(std::format("MD0{};", ModeCode(mode)));
      rig->Enqueue(std::format("FA{:09d};", hz));
    }
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","freq":{},"mode":"{}"}})", hz, mode));
  });

  // ⚠️ DIGITS ONLY. The buffer is parsed as a number later, so a non-digit
  // silently parsed to 0 and tuned the rig to the bottom of its range - the
  // reference host carries that same comment, learned the same way.
  server.GetPrefix("/api/freq/digit/", [host](const std::string& d, const HttpRequest&,
                                              HttpResponse& res) {
    if (d.empty() || d.find_first_not_of("0123456789") != std::string::npos) {
      WriteJson(res, 400, R"({"status":"error","message":"digits only"})");
      return;
    }
    std::string buf;
    if (host) {
      std::lock_guard<std::mutex> lock(host->mu);
      host->freq_buffer += d;
      buf = host->freq_buffer;
    }
    WriteJson(res, 200, OkJson("buffer", Quoted(buf)));
  });

  // /api/step/<hz>/<up|down>
  server.GetPrefix("/api/step/", [rig](const std::string& suffix, const HttpRequest&,
                                       HttpResponse& res) {
    const auto slash = suffix.find('/');
    if (slash == std::string::npos) {
      WriteJson(res, 400, R"({"status":"error","message":"expected <hz>/<up|down>"})");
      return;
    }
    long long hz = 0;
    try { hz = std::stoll(suffix.substr(0, slash)); } catch (const std::exception&) {
      WriteJson(res, 400, R"({"status":"error","message":"step is not a number"})");
      return;
    }
    const std::string dir = suffix.substr(slash + 1);
    if (dir != "up" && dir != "down") {
      WriteJson(res, 400, R"({"status":"error","message":"direction must be up or down"})");
      return;
    }
    const long long delta = (dir == "down") ? -hz : hz;
    // Read-modify-write: stepping needs the current frequency, so it runs as one
    // task on the poller thread rather than as a read here and a write there.
    if (rig) rig->EnqueueTask([delta](CatTransport& cat) {
      auto fa = cat.Exchange("FA;");
      if (!fa || fa->size() < 12) return;
      long long f = std::stoll(fa->substr(2, 9)) + delta;
      if (f < 0) f = 0;
      cat.Send(std::format("FA{:09d};", f));
    });
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","step":{},"direction":"{}"}})", hz, dir));
  });

  server.GetPrefix("/api/volume/set/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int pct = 0;
    if (!parse_int(s, pct)) { bad_request(res, "volume is not a number"); return; }
    pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (rig) rig->Enqueue(std::format("AG0{:03d};", pct * 255 / 100));
    WriteJson(res, 200, OkJson("volume", std::to_string(pct)));
  });

  server.GetPrefix("/api/rf-gain/set/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int pct = 0;
    if (!parse_int(s, pct)) { bad_request(res, "rf_gain is not a number"); return; }
    pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (rig) rig->Enqueue(std::format("RG0{:03d};", pct * 255 / 100));
    WriteJson(res, 200, OkJson("rf_gain", std::to_string(pct)));
  });

  server.GetPrefix("/api/cw-speed/set/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int wpm = 0;
    if (!parse_int(s, wpm)) { bad_request(res, "wpm is not a number"); return; }
    if (rig) rig->Enqueue(std::format("KS{:03d};", wpm));
    WriteJson(res, 200, OkJson("wpm", std::to_string(wpm)));
  });

  // ⚠️ The power cap applies here too, on the same is_local rule as
  // /api/power/max. Capping the preset buttons but not the numeric setter would
  // leave the cap trivially bypassable.
  server.GetPrefix("/api/power/set/", [rig, trusted, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int w = 0;
    if (!parse_int(s, w)) { bad_request(res, "power is not a number"); return; }
    bool clamped = false;
    if (trusted && w > kLocalPowerCap) { w = kLocalPowerCap; clamped = true; }
    if (w < 0) w = 0;
    if (w > kMaxWatts) w = kMaxWatts;
    if (rig) rig->Enqueue("PC" + Pad(w, 3) + ";");
    WriteJson(res, 200,
              std::format(R"({{"status":"ok","power":{},"clamped":{}}})", w, JsonBool(clamped)));
  });

  server.GetPrefix("/api/remote-tx/gain/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int g = 0;
    if (!parse_int(s, g)) { bad_request(res, "gain is not a number"); return; }
    g = g < 0 ? 0 : (g > 100 ? 100 : g);
    if (rig) rig->Enqueue(std::format("EX010113{:03d};", g));
    WriteJson(res, 200, OkJson("rport_gain", std::to_string(g)));
  });

  server.GetPrefix("/api/ssb-out-level/set/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int lv = 0;
    if (!parse_int(s, lv)) { bad_request(res, "level is not a number"); return; }
    lv = lv < 0 ? 0 : (lv > 100 ? 100 : lv);
    if (rig) rig->Enqueue(std::format("EX010109{:03d};", lv));
    WriteJson(res, 200, OkJson("ssb_out_level", std::to_string(lv)));
  });

  server.GetPrefix("/api/memory/recall/", [rig, parse_int, bad_request](
      const std::string& s, const HttpRequest&, HttpResponse& res) {
    int m = 0;
    if (!parse_int(s, m)) { bad_request(res, "memory is not a number"); return; }
    if (rig) rig->Enqueue(std::format("MC{:03d};", m));
    WriteJson(res, 200, OkJson("memory", std::to_string(m)));
  });

  // ⚠️ Amp tune again, this time as a prefix. The refusal has to be repeated
  // here: a caller reaching /api/tune/amp/anything must not slip past the exact
  // route's check.
  server.GetPrefix("/api/tune/amp/", [trusted](const std::string&, const HttpRequest&,
                                               HttpResponse& res) {
    if (!trusted) {
      WriteJson(res, 200,
                R"({"status":"error",)"
                R"("message":"Amp tune is only available when connected locally."})");
      return;
    }
    WriteJson(res, 200,
              R"({"status":"error","available":false,"tuner":"amp",)"
              R"("message":"Amp tuner is not configured on this host"})");
  });

  // Not-configured features answer honestly rather than 404, so a client can
  // tell "this host cannot do it" from "this host has never heard of it".
  server.GetPrefix("/api/rxant/", [](const std::string&, const HttpRequest&,
                                     HttpResponse& res) {
    WriteJson(res, 200,
              R"({"status":"error","available":false,)"
              R"("message":"RX antenna switch is not configured - set kmtronic_host"})");
  });
  server.GetPrefix("/api/preset/", [](const std::string& name, const HttpRequest&,
                                      HttpResponse& res) {
    WriteJson(res, 200,
              std::format(R"({{"status":"error","message":"Preset '{}' not found"}})", name));
  });
  auto keyer_absent = [](const char* what) {
    return [what](const std::string&, const HttpRequest&, HttpResponse& res) {
      WriteJson(res, 200,
                std::format(R"({{"status":"error","available":false,)"
                            R"("message":"{} keyer is not implemented in the C++ host yet"}})",
                            what));
    };
  };
  server.GetPrefix("/api/cw/memory/", keyer_absent("CW"));
  server.GetPrefix("/api/cw/send/", keyer_absent("CW"));
  server.GetPrefix("/api/voice/play/", keyer_absent("Voice"));

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

  // ── TX audio ───────────────────────────────────────────────────────────────
  // ⚠️ THIS IS THE ROUTE THAT PUTS A VOICE ON THE AIR, so it has two gates the
  // RX stream does not need.
  if (deps.tx_audio) {
    TxAudioReceiver* tx = deps.tx_audio;
    RadioPoller* rigp = deps.poller;
    AuthService* auth = deps.auth;

    // ⚠️ Which connection holds the claim, tracked PER CONNECTION.
    // Releasing "whoever currently holds it" on close is a real bug: a second
    // client that was REFUSED still gets a close callback, and that close would
    // hand away the first client's transmitter mid-over. Only the connection
    // that actually claimed it may release it.
    auto claims = std::make_shared<std::map<WsConnection*, std::string>>();
    auto claims_mu = std::make_shared<std::mutex>();
    server.WebSocketRoute(
        "/ws/tx",
        [tx, auth, trusted, claims, claims_mu](const HttpRequest& req,
                                              std::shared_ptr<WsConnection> c) {
          const std::string token = ExtractToken(req);
          const std::string who = auth ? auth->Username(token).value_or("") : "";

          // GATE 1: can_transmit. A session is not enough - the reference host
          // carries a per-user transmit permission and a listener must not be
          // able to key the rig just because they could log in.
          const bool may_transmit = trusted || (auth && auth->CanTransmit(token));
          if (!may_transmit) {
            c->SendText(R"({"type":"error","message":"not permitted to transmit"})");
            c->MarkClosed();
            return;
          }
          // GATE 2: one transmitter. Two clients feeding the rig would
          // interleave two voices into one carrier.
          const std::string id = who.empty() ? "local" : who;
          if (!tx->Claim(id)) {
            c->SendText(R"({"type":"error","message":"another client is transmitting"})");
            c->MarkClosed();
            return;
          }
          {
            std::lock_guard<std::mutex> lock(*claims_mu);
            (*claims)[c.get()] = id;
          }
          c->SendText(std::format(
              R"({{"type":"config","sample_rate":{},"channels":1,"bits_per_sample":16}})",
              tx->SampleRate()));
        },
        [tx, rigp, claims, claims_mu](std::shared_ptr<WsConnection> c) {
          std::string id;
          {
            std::lock_guard<std::mutex> lock(*claims_mu);
            auto it = claims->find(c.get());
            if (it == claims->end()) return;   // never claimed - nothing to release
            id = it->second;
            claims->erase(it);
          }
          tx->Release(id);

          // ⚠️ DROP POWER BACK TO THE LOCAL CAP WHEN THE REMOTE CLIENT GOES.
          //
          // Remote operating runs at up to 200 W. The operator then sits down at
          // the radio itself, keys up, and drives an amplifier with twice the
          // power they expect - because a setting made from another room is
          // still in force and nothing on the front panel says a client put it
          // there.
          //
          // ⚠️ IT LIVES HERE, NEXT TO THE RADIO, for the same reason the
          // transmit watchdog does: a client-side reset protects nothing when
          // the client is the thing that died. This fires on a clean disconnect,
          // a crash and a dropped link alike, because all three close the socket.
          //
          // Only when the connection actually HELD the transmitter - a refused
          // second client must not reach in and change the first one's power.
          if (rigp) {
            // ⚠️ AND HAND THE MICROPHONE BACK. Remote TX leaves the rig on
            // SSB MOD SOURCE=REAR / REAR SELECT=USB, and on REAR the operator's
            // own hand mic at the radio does NOTHING - it keys, ALC sits at its
            // idle floor, and no power comes out. That is the exact symptom that
            // cost a night to diagnose from the other direction. A remote
            // session must not leave the station unusable to the person standing
            // in front of it.
            //
            // ⚠️ 50 ms BETWEEN THE WRITES. Sent back to back the rig takes the
            // first and ignores the rest, silently (§8g). One task so the
            // sequence cannot be interleaved with anything else on the port.
            rigp->EnqueueTask([](CatTransport& cat) {
              cat.Send("PC" + Pad(kLocalPowerCap, 3) + ";");
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
              cat.Send("EX0101110;");   // SSB MOD SOURCE -> MIC
            });
            hdlog::Line(hdlog::kInfo, "TX",
                        "client " + id + " disconnected - power back to " +
                            std::to_string(kLocalPowerCap) +
                            " W and MOD SOURCE back to MIC");
          }
        },
        [tx, rigp](std::shared_ptr<WsConnection>, const char* data, size_t len,
                   bool is_binary) {
          if (!is_binary) return true;   // text frames are control, not audio
          const bool keyed = rigp && rigp->Snapshot().tx;
          return tx->Accept(data, len, keyed);
        });
  }

  // ── Administration ─────────────────────────────────────────────────────────
  // The admin gate above has already run; nothing here re-checks it.
  {
    AuthService* auth = deps.auth;
    Config* cfg = deps.config;
    auto persist = deps.save_config;

    // Mirror the in-memory user list into the config and write it out.
    // ⚠️ Without this an added user exists until the next restart, which on a
    // station host is the next power cut rather than a maintenance window.
    auto persist_users = [auth, cfg, persist](std::string& err) {
      if (!auth || !cfg || !persist) {
        err = "config is not writable on this host";
        return false;
      }
      cfg->web_users.clear();
      for (const auto& u : auth->ListUsers()) {
        ConfigUser cu;
        cu.username = u.username;
        cu.is_admin = u.is_admin;
        cu.can_transmit = u.can_transmit;
        cu.password_hash = auth->PasswordHashOf(u.username);
        cfg->web_users.push_back(cu);
      }
      return persist(err);
    };

    server.Get("/api/admin/users", [auth](const HttpRequest&, HttpResponse& res) {
      std::string rows;
      if (auth) {
        for (const auto& u : auth->ListUsers()) {
          if (!rows.empty()) rows += ",";
          rows += std::format(R"({{"username":"{}","is_admin":{},"can_transmit":{}}})",
                              u.username, JsonBool(u.is_admin), JsonBool(u.can_transmit));
        }
      }
      WriteJson(res, 200, std::format(R"({{"status":"ok","users":[{}]}})", rows));
    });

    server.Get("/api/admin/sessions", [auth](const HttpRequest&, HttpResponse& res) {
      std::string rows;
      if (auth) {
        for (const auto& x : auth->ListSessions()) {
          if (!rows.empty()) rows += ",";
          // ⚠️ token_short only - a full session token in an admin listing is a
          // credential in a log, a screenshot and a support ticket.
          rows += std::format(
              R"({{"token_short":"{}","username":"{}","is_admin":{},)"
              R"("can_transmit":{},"idle_seconds":{}}})",
              x.token_short, x.username, JsonBool(x.is_admin),
              JsonBool(x.can_transmit), x.idle_seconds);
        }
      }
      WriteJson(res, 200, std::format(R"({{"status":"ok","sessions":[{}]}})", rows));
    });

    server.Post("/api/admin/user/add", [auth, persist_users](const HttpRequest& req,
                                                             HttpResponse& res) {
      const std::string user = JsonField(req.body, "username");
      const std::string pass = JsonField(req.body, "password");
      const bool is_admin = req.body.find("\"is_admin\":true") != std::string::npos;
      const bool can_tx = req.body.find("\"can_transmit\":false") == std::string::npos;
      if (user.empty() || pass.empty()) {
        WriteJson(res, 400,
                  R"({"status":"error","message":"username and password are required"})");
        return;
      }
      // ⚠️ Hashed here, immediately. A plaintext password must never reach the
      // config file, and the only way to guarantee that is never to store one.
      auth->AddUser(user, AuthService::HashPassword(pass), is_admin, can_tx);
      std::string err;
      if (!persist_users(err)) {
        WriteJson(res, 500,
                  std::format(R"({{"status":"error","message":"user added but NOT saved: {}"}})",
                              err));
        return;
      }
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","message":"User '{}' added"}})", user));
    });

    server.Post("/api/admin/user/password", [auth, persist_users](const HttpRequest& req,
                                                                  HttpResponse& res) {
      const std::string user = JsonField(req.body, "username");
      const std::string pass = JsonField(req.body, "password");
      if (user.empty() || pass.empty()) {
        WriteJson(res, 400,
                  R"({"status":"error","message":"username and password are required"})");
        return;
      }
      if (!auth->ChangePassword(user, AuthService::HashPassword(pass))) {
        WriteJson(res, 404, R"({"status":"error","message":"no such user"})");
        return;
      }
      std::string err;
      // ⚠️ A password change that is not saved is the worst of the three: the
      // operator sets a new one, it works, and the OLD password comes back at
      // the next restart - with the new one having been believed and written
      // down somewhere. See the note on /api/admin/user/remove/.
      if (!persist_users(err)) {
        WriteJson(res, 500,
                  std::format(R"({{"status":"error","message":"password changed on the )"
                              R"(running host but NOT saved - the old one returns on )"
                              R"(restart: {}"}})", err));
        return;
      }
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","message":"Password changed for '{}'"}})",
                            user));
    });

    server.GetPrefix("/api/admin/user/remove/",
                     [auth, persist_users](const std::string& user, const HttpRequest&,
                                           HttpResponse& res) {
      // ⚠️ REFUSE TO REMOVE THE LAST ADMIN. The reference host does not check
      // this. Removing the only admin leaves a host nobody can administer -
      // recoverable only by editing a config file by hand and restarting, on a
      // box that may be at the far end of a radio link.
      bool target_is_admin = false;
      for (const auto& u : auth->ListUsers()) {
        if (u.username == user) target_is_admin = u.is_admin;
      }
      if (target_is_admin && auth->AdminCount() <= 1) {
        WriteJson(res, 409,
                  R"({"status":"error","message":"refusing to remove the last admin - )"
                  R"(the host would have no one who could administer it"})");
        return;
      }
      if (!auth->RemoveUser(user)) {
        WriteJson(res, 404, R"({"status":"error","message":"no such user"})");
        return;
      }
      std::string err;
      // ⚠️ REPORT A FAILED SAVE. This used to discard `err` and answer "ok"
      // regardless, so a removal that never reached disk looked identical to one
      // that did - and the user walked back in at the next restart, which on a
      // station host is the next power cut. /api/admin/user/add has always
      // checked this; these routes did not, which is the worse half: an account
      // you believe is gone is not the same kind of mistake as one you believe
      // is missing.
      if (!persist_users(err)) {
        WriteJson(res, 500,
                  std::format(R"({{"status":"error","message":"user removed from the )"
                              R"(running host but NOT saved - it returns on restart: {}"}})",
                              err));
        return;
      }
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","message":"User '{}' removed"}})", user));
    });

    // /api/admin/user/tx/enable/<user> and .../disable/<user>
    server.GetPrefix("/api/admin/user/tx/",
                     [auth, persist_users](const std::string& suffix, const HttpRequest&,
                                           HttpResponse& res) {
      const auto slash = suffix.find('/');
      if (slash == std::string::npos) {
        WriteJson(res, 400,
                  R"({"status":"error","message":"expected enable|disable/<username>"})");
        return;
      }
      const std::string verb = suffix.substr(0, slash);
      const std::string user = suffix.substr(slash + 1);
      if (verb != "enable" && verb != "disable") {
        WriteJson(res, 400, R"({"status":"error","message":"expected enable or disable"})");
        return;
      }
      const bool allow = (verb == "enable");
      if (!auth->SetCanTransmit(user, allow)) {
        WriteJson(res, 404, R"({"status":"error","message":"no such user"})");
        return;
      }
      std::string err;
      // ⚠️ Same reason as the two routes above - and here the unsaved change is
      // a TRANSMIT right. Revoking one that quietly returns at the next restart
      // is a permission you believe you took away and did not.
      if (!persist_users(err)) {
        WriteJson(res, 500,
                  std::format(R"({{"status":"error","message":"transmit right changed on )"
                              R"(the running host but NOT saved - it reverts on restart: )"
                              R"({}"}})", err));
        return;
      }
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","username":"{}","can_transmit":{}}})",
                            user, JsonBool(allow)));
    });

    server.GetPrefix("/api/admin/kick/", [auth](const std::string& user,
                                                const HttpRequest&, HttpResponse& res) {
      const int n = auth->KillUserSessions(user);
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","kicked":{},)"
                            R"("message":"Killed {} sessions for '{}'"}})", n, n, user));
    });

    auto lockdown = [auth, cfg, persist](bool on) {
      return [auth, cfg, persist, on](const HttpRequest&, HttpResponse& res) {
        (void)auth;
        if (cfg) cfg->admin_only_login = on;
        std::string err;
        if (persist) persist(err);
        WriteJson(res, 200,
                  std::format(R"({{"status":"ok","admin_only_login":{},"message":"{}"}})",
                              JsonBool(on),
                              on ? "Login restricted to admins" : "All users may log in"));
      };
    };
    server.Get("/api/admin/lockdown/on", lockdown(true));
    server.Get("/api/admin/lockdown/off", lockdown(false));
    server.Get("/api/admin/lockdown/status", [cfg](const HttpRequest&, HttpResponse& res) {
      WriteJson(res, 200,
                std::format(R"({{"status":"ok","admin_only_login":{}}})",
                            JsonBool(cfg && cfg->admin_only_login)));
    });
  }

  server.Post("/api/auth/logout", [&deps](const HttpRequest& req, HttpResponse& res) {
    if (deps.auth) deps.auth->Logout(ExtractToken(req));
    res.extra_headers.push_back({"Set-Cookie", "hamdeck_session=; Path=/; HttpOnly; Max-Age=0"});
    WriteJson(res, 200, R"({"status":"ok","message":"Logged out"})");
  });
}
