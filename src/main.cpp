// HamDeck C++ host.
//
// Route surface is in api.cpp; this file owns process startup and the listener
// split. See WIP.md for the road map and CARRYOVER.md for the traps.

#include <httplib.h>

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "api.h"
#include "auth.h"
#include "cat_sim.h"
#include "radio.h"
#include "version.h"

namespace {

// TWO LISTENERS, AND THE SPLIT IS LOAD-BEARING. Measured against the running C#
// host on the VM on 08/30/2026:
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

  // Simulated rig: the real CAT bridge is passed through to the VM and the
  // station is on the air. The backend is named in the banner so a simulator
  // reading is never mistaken for the radio.
  RadioPoller poller(std::make_unique<SimulatedRig>());
  poller.Start();

  AuthService auth(480);
  if (const char* hash = std::getenv("HAMDECK_ADMIN_HASH")) {
    auth.AddUser("admin", hash, /*is_admin=*/true);
  }

  ApiDeps deps;
  deps.poller = &poller;
  deps.auth = &auth;
  deps.allow_anonymous_status = false;   // matches the live station

  httplib::Server control;
  httplib::Server dashboard;
  InstallRoutes(control, Listener::kControl, kControlPort, deps);
  InstallRoutes(dashboard, Listener::kDashboard, kDashPort, deps);

  std::cout << kServiceName << ' ' << kVersion << '\n'
            << "CAT backend: " << poller.Backend() << '\n'
            << "auth: " << (auth.IsConfigured() ? "configured"
                                                : "NO USERS - dashboard will 401")
            << '\n' << std::flush;

  std::thread control_thread([&] {
    std::cout << "control   " << kControlAddr << ':' << kControlPort
              << " (local only, no session)\n" << std::flush;
    if (!control.listen(kControlAddr, kControlPort)) {
      std::cerr << "failed to bind " << kControlAddr << ':' << kControlPort << '\n';
      std::exit(1);
    }
  });

  std::cout << "dashboard " << kDashAddr << ':' << kDashPort
            << " (session required)\n" << std::flush;
  if (!dashboard.listen(kDashAddr, kDashPort)) {
    std::cerr << "failed to bind " << kDashAddr << ':' << kDashPort << '\n';
    return 1;
  }
  control_thread.join();
  return 0;
}
