// HamDeck C++ host - scaffold.
//
// Scope right now: /api/health only. It is deliberately the first route: it is
// the one route the client may call with no session (CARRYOVER.md section 2),
// so it is the only thing provable end to end before auth exists.
//
// No radio is attached to this VM - see WIP.md. Every field describing hardware
// reports its real absent state rather than a plausible default. The C# Linux
// host answering "ok" for things it was not doing is the exact bug this port
// exists not to repeat (CARRYOVER.md section 1, /api/record/start).

#include <httplib.h>

#include <csignal>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <thread>

#include "version.h"

namespace {

// TWO LISTENERS, AND THE SPLIT IS LOAD-BEARING. Measured against the running C#
// host on the VM on 08/30/2026:
//
//   :5001  control API  - 127.0.0.1 ONLY. A LAN caller is refused:
//          "The control API only serves this machine."
//   :5002  dashboard    - LAN reachable. /api/health needs no session; the rest do.
//
// This is what makes "local" mean something. /api/tune/amp must refuse every
// remote caller (CARRYOVER.md section 2), and the only trustworthy way to know a
// caller is local is WHICH SOCKET ACCEPTED IT. Never infer it from a header -
// X-Forwarded-For and friends are attacker-controlled. Bind the control server to
// the loopback address and the guarantee is enforced by the kernel, not by a
// check somebody can forget to write.
constexpr const char* kControlAddr = "127.0.0.1";
constexpr int         kControlPort = 5001;
constexpr const char* kDashAddr    = "0.0.0.0";
constexpr int         kDashPort    = 5002;

// The rig is not reachable from this VM: the CAT bridge is passed through to
// the VM. Until the serial layer exists this is honestly false, never optimistic.
bool rig_connected() { return false; }

// Field names and types match the C# host exactly so the existing WPF client's
// capability probe is unchanged.
//
// One deliberate difference: "port" reports the port actually bound. The C# host
// hardcodes 5001 and so answers "port":5001 on 5002 too, which makes the field
// useless for telling the two listeners apart - verified on the VM.
std::string health_json(int bound_port) {
  return std::format(
      R"({{"status":"ok","service":"{}","version":"{}","port":{},)"
      R"("rig_connected":{},"amp_tuning":{},"tgxl_tuning":{},"freq_buffer":""}})",
      kServiceName, kVersion, bound_port,
      rig_connected() ? "true" : "false",
      "false",   // amp tuning: no amp wired to this host yet
      "false");  // TGXL tuning: same
}

void install_routes(httplib::Server& server, int bound_port, const char* role) {
  server.Get("/api/health", [bound_port](const httplib::Request&, httplib::Response& res) {
    res.set_content(health_json(bound_port), "application/json");
  });

  server.set_pre_routing_handler(
      [role](const httplib::Request& req, httplib::Response&) {
        std::cout << role << ' ' << req.method << ' ' << req.path << '\n' << std::flush;
        return httplib::Server::HandlerResponse::Unhandled;
      });
}

}  // namespace

int main() {
  std::signal(SIGPIPE, SIG_IGN);

  httplib::Server control;
  httplib::Server dashboard;
  install_routes(control, kControlPort, "control");
  install_routes(dashboard, kDashPort, "dash");

  std::thread control_thread([&] {
    std::cout << "control API on " << kControlAddr << ':' << kControlPort
              << " (local only)\n" << std::flush;
    if (!control.listen(kControlAddr, kControlPort)) {
      std::cerr << "failed to bind " << kControlAddr << ':' << kControlPort << '\n';
      std::exit(1);
    }
  });

  std::cout << kServiceName << ' ' << kVersion << '\n' << std::flush;
  std::cout << "dashboard on " << kDashAddr << ':' << kDashPort << '\n' << std::flush;
  if (!dashboard.listen(kDashAddr, kDashPort)) {
    std::cerr << "failed to bind " << kDashAddr << ':' << kDashPort << '\n';
    return 1;
  }
  control_thread.join();
  return 0;
}
