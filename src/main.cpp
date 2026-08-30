// HamDeck C++ host.
//
// Route surface is in api.cpp; this file owns process startup and the listener
// split. See WIP.md for the road map and CARRYOVER.md for the traps.

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

#include "api.h"
#include "audio.h"
#include "http.h"
#include "auth.h"
#include "cat_sim.h"
#include "config.h"
#include "serial_cat.h"
#include "radio.h"
#include "version.h"

namespace {

// TWO LISTENERS, AND THE SPLIT IS LOAD-BEARING. Measured against the running C#
// host on the reference host on 08/30/2026:
//
//   :5001 control   - 127.0.0.1 ONLY, no session required. A LAN caller is
//                     refused: "The control API only serves this machine."
//   :5002 dashboard - LAN. Only /api/health and /api/auth/status are anonymous;
//                     everything else is 401 without a session (verified).
//
// The control port trusting every caller is exactly why it must be bound to the
// loopback address: the trust boundary is then enforced by the kernel, not by a
// check somebody can forget to write. /api/tune/amp must refuse every remote
// caller (CARRYOVER.md section 2), and "is this caller local" is answered by
// WHICH SOCKET ACCEPTED IT - never by a header, which the caller controls.
constexpr const char* kControlAddr = "127.0.0.1";
constexpr int         kControlPort = 5001;
constexpr const char* kDashAddr    = "0.0.0.0";
constexpr int         kDashPort    = 5002;

}  // namespace

// Walks the whole startup path - poller, audio, auth, both listeners - proves the
// process actually serves a request, and exits. CARRYOVER.md section 8: the .NET
// client shipped a release that could not launch at all while every test passed,
// because CI built the artifact and never ran it.
//
// ⚠️ A HANG IS A FAILURE, not a pass. CI must run this under an external timeout;
// a selftest that blocks forever looks exactly like one that is still working.
int SelfTest(RadioPoller& poller, RxAudioStream& rx, HttpServer& control,
             const std::string& control_spec);

int main(int argc, char** argv) {
  bool selftest = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--selftest") {
      selftest = true;
    } else {
      // Unknown flags abort rather than being ignored. Silently accepting a
      // misspelled flag is how a safety option gets quietly disabled.
      std::cerr << "unknown argument: " << arg
                << "\nusage: hamdeck-host [--selftest]\n"
                   "  HAMDECK_CAT_DEVICE=<path>|auto   talk to the radio "
                   "(default: simulated rig)\n"
                   "  HAMDECK_ADMIN_HASH=pbkdf2:...    admin credential\n";
      return 2;
    }
  }

  // A handler that swallows SIGTERM without stopping the server makes the
  // process unkillable by normal means and breaks every systemctl restart. It
  // happened here once already. Default disposition is correct; only SIGPIPE
  // needs ignoring, so a dropped client cannot kill the host.
  std::signal(SIGPIPE, SIG_IGN);

  // ── Config ────────────────────────────────────────────────────────────────
  // A missing file is fine: the defaults are usable and name no station. A
  // file that EXISTS but is malformed is fatal - starting on defaults would run
  // the station on settings the operator did not choose and believes they
  // changed, including the transmit watchdog.
  Config config;
  for (const auto& path : Config::DefaultPaths()) {
    std::string err;
    if (Config::Load(path, config, err)) {
      std::cout << "config: " << path << '\n' << std::flush;
      break;
    }
    if (err != "not found") {
      std::cerr << "FATAL: config " << path << ": " << err << '\n';
      return 1;
    }
  }

  // ⚠️ THE DEFAULT IS THE SIMULATOR, DELIBERATELY.
  // Talking to the radio has to be asked for. A host that hunts for a serial port
  // on startup would grab the CAT link the moment it ran anywhere near the
  // station - including on a laptop, in CI, or on a box that was only meant to be
  // built on. Set HAMDECK_CAT_DEVICE to a device path, or "auto" to probe the
  // usual candidates with ID; and nothing else.
  //
  // The backend is named in the banner and in /api/health's describe path, so a
  // simulator reading is never mistaken for the radio.
  std::unique_ptr<CatTransport> cat;
  bool simulated = false;
  const char* env_dev = std::getenv("HAMDECK_CAT_DEVICE");
  const std::string dev_str = env_dev ? env_dev : config.radio_port;
  if (!dev_str.empty()) {
    const char* dev = dev_str.c_str();
    auto serial = std::make_unique<SerialCat>();
    const bool ok =
        (std::string(dev) == "auto")
            ? serial->OpenFirstResponding({"/dev/ttyUSB0", "/dev/ttyUSB1"})
            : serial->Open(dev, config.radio_baud);
    if (!ok) {
      // Fail loudly and exit. Falling back to the simulator here would be the
      // worst possible behaviour: the host would come up looking healthy and
      // report a rig that is not there.
      std::cerr << "FATAL: could not open CAT device '" << dev
                << "'. Not falling back to the simulator - a host that reports a "
                   "rig it cannot reach is worse than one that refuses to start.\n";
      return 1;
    }
    std::cout << "CAT: " << serial->Describe() << '\n' << std::flush;
    cat = std::move(serial);
  } else {
    cat = std::make_unique<SimulatedRig>();
    simulated = true;
  }
  RadioPoller poller(std::move(cat));
  poller.SetPttTimeoutSeconds(config.ptt_timeout_seconds);
  poller.Start();

  AuthService auth(config.web_session_timeout);
  for (const auto& u : config.web_users) {
    auth.AddUser(u.username, u.password_hash, u.is_admin, u.can_transmit);
  }
  // Env override, for a throwaway run without writing a config file. It does not
  // replace the configured users, it adds to them.
  if (const char* hash = std::getenv("HAMDECK_ADMIN_HASH")) {
    auth.AddUser("admin", hash, /*is_admin=*/true);
  }

  // Synthetic RX audio: the codec is passed through to the reference host, so
  // there is no real capture device here. 22050 Hz mono/16-bit matches the wire
  // format the client expects (CARRYOVER.md section 2).
  RxAudioStream rx_audio(std::make_unique<ToneSource>(config.record_sample_rate, 700.0));
  rx_audio.Start();

  ApiDeps deps;
  deps.poller = &poller;
  deps.auth = &auth;
  deps.rx_audio = &rx_audio;
  deps.simulated = simulated;
  deps.allow_anonymous_status = config.allow_anonymous_status;

  HttpServer control;
  HttpServer dashboard;
  InstallRoutes(control, Listener::kControl, kControlPort, deps);
  InstallRoutes(dashboard, Listener::kDashboard, kDashPort, deps);

  std::cout << kServiceName << ' ' << kVersion << '\n'
            << "CAT backend: " << poller.Backend() << '\n'
            << "rx audio: " << rx_audio.Backend() << '\n'
            << "watchdog: " << (config.ptt_timeout_seconds > 0
                   ? std::to_string(config.ptt_timeout_seconds) + "s" : "DISABLED") << '\n'
            << "auth: " << (auth.IsConfigured() ? "configured"
                                                : "NO USERS - dashboard will 401")
            << '\n' << std::flush;

  // civetweb listens on its own threads, so both Listen() calls return at once.
  const std::string control_spec =
      std::string(kControlAddr) + ":" + std::to_string(kControlPort);
  const std::string dash_spec = std::string(kDashAddr) + ":" + std::to_string(kDashPort);

  if (!control.Listen(control_spec)) {
    std::cerr << "failed to bind " << control_spec << '\n';
    return 1;
  }
  std::cout << "control   " << control_spec << " (local only, no session)\n" << std::flush;

  if (!dashboard.Listen(dash_spec)) {
    std::cerr << "failed to bind " << dash_spec << '\n';
    return 1;
  }
  std::cout << "dashboard " << dash_spec << " (session required)\n" << std::flush;

  if (selftest) return SelfTest(poller, rx_audio, control, control_spec);

  // Park the main thread. SIGTERM ends the process and civetweb's threads with
  // it - deliberately no signal handler, see the note above.
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
}

namespace {

bool Probe(const std::string& host, int port, const std::string& path, std::string& body) {
  const std::string cmd = "curl -sS --max-time 5 http://" + host + ":" +
                          std::to_string(port) + path;
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return false;
  char buf[1024];
  body.clear();
  while (fgets(buf, sizeof(buf), f)) body += buf;
  return pclose(f) == 0 && !body.empty();
}

}  // namespace

int SelfTest(RadioPoller& poller, RxAudioStream& rx, HttpServer&,
             const std::string&) {
  int failures = 0;
  auto check = [&](const char* what, bool ok) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n' << std::flush;
    if (!ok) ++failures;
  };

  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  check("poller has polled", poller.CacheAgeMs() >= 0);
  check("poller cache is fresh", poller.CacheAgeMs() < RadioPoller::kStaleAfterMs);
  check("rig reports connected", poller.Snapshot().connected);
  check("audio config frame well formed",
        rx.ConfigJson().find("\"sample_rate\":22050") != std::string::npos);

  std::string body;
  const bool got = Probe("127.0.0.1", kControlPort, "/api/health", body);
  check("control port answers /api/health", got);
  check("health says ok", body.find("\"status\":\"ok\"") != std::string::npos);

  const bool got_status = Probe("127.0.0.1", kControlPort, "/api/status", body);
  check("control port answers /api/status", got_status);

  std::cout << (failures ? "SELFTEST FAILED" : "SELFTEST PASSED") << '\n' << std::flush;
  return failures ? 1 : 0;
}
