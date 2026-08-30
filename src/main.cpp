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

int main() {
  // A handler that swallows SIGTERM without stopping the server makes the
  // process unkillable by normal means and breaks every systemctl restart. It
  // happened here once already. Default disposition is correct; only SIGPIPE
  // needs ignoring, so a dropped client cannot kill the host.
  std::signal(SIGPIPE, SIG_IGN);

  // Simulated rig: the real CAT bridge is passed through to the reference host and the
  // station is on the air. The backend is named in the banner so a simulator
  // reading is never mistaken for the radio.
  RadioPoller poller(std::make_unique<SimulatedRig>());
  poller.Start();

  AuthService auth(480);
  if (const char* hash = std::getenv("HAMDECK_ADMIN_HASH")) {
    auth.AddUser("admin", hash, /*is_admin=*/true);
  }

  // Synthetic RX audio: the codec is passed through to the reference host, so
  // there is no real capture device here. 22050 Hz mono/16-bit matches the wire
  // format the client expects (CARRYOVER.md section 2).
  RxAudioStream rx_audio(std::make_unique<ToneSource>(22050, 700.0));
  rx_audio.Start();

  ApiDeps deps;
  deps.poller = &poller;
  deps.auth = &auth;
  deps.rx_audio = &rx_audio;
  deps.allow_anonymous_status = false;   // matches the live station

  HttpServer control;
  HttpServer dashboard;
  InstallRoutes(control, Listener::kControl, kControlPort, deps);
  InstallRoutes(dashboard, Listener::kDashboard, kDashPort, deps);

  std::cout << kServiceName << ' ' << kVersion << '\n'
            << "CAT backend: " << poller.Backend() << '\n'
            << "rx audio: " << rx_audio.Backend() << '\n'
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

  // Park the main thread. SIGTERM ends the process and civetweb's threads with
  // it - deliberately no signal handler, see the note above.
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
}
