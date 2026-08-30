// Config loading.
//
// The assertions that matter are the REFUSALS. A config layer that falls back to
// defaults on a bad file runs the station on settings the operator did not choose
// and believes they changed - which is worse than not starting.

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "../src/config.h"

namespace {

std::string Write(const std::string& name, const std::string& body) {
  const std::string path = "/tmp/hamdeck-test-" + name + ".json";
  std::ofstream(path) << body;
  return path;
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string err;

  // Defaults must name no station. This is the public-repo rule as a test.
  Config d;
  assert(d.tgxl_host.empty());
  assert(d.kmtronic_host.empty());
  assert(d.radio_port.empty());
  std::printf("defaults: ship no host, no address, no serial port\n");
  assert(d.ptt_timeout_seconds == 180);
  assert(d.record_sample_rate == 22050);
  assert(!d.allow_anonymous_status);
  std::printf("defaults: watchdog 180s, RX 22050Hz, anonymous status off\n");

  // A missing file is fine; defaults are usable.
  Config missing;
  assert(!Config::Load("/tmp/definitely-not-here.json", missing, err));
  assert(err == "not found");
  std::printf("missing:  reported as not found, not as corrupt\n");

  // A good file.
  Config ok;
  const auto good = Write("good", R"({
    "ptt_timeout_seconds": 90,
    "allow_anonymous_status": true,
    "web_session_timeout": 60,
    "web_users": [
      {"username":"joe","password_hash":"pbkdf2:aa:bb","is_admin":true,"can_transmit":true}
    ]
  })");
  assert(Config::Load(good, ok, err));
  assert(ok.ptt_timeout_seconds == 90);
  assert(ok.allow_anonymous_status);
  assert(ok.web_users.size() == 1 && ok.web_users[0].username == "joe");
  assert(ok.web_users[0].is_admin);
  assert(ok.record_sample_rate == 22050);   // untouched keys keep their default
  std::printf("load:     values applied, untouched keys keep defaults\n");

  // ── Refusals ──────────────────────────────────────────────────────────────
  // ⚠️ A FRESH Config per case. Sharing one made the port-collision case pass
  // for the wrong reason: a rejected value from the previous case was still
  // present and tripped a different check entirely.
  Config bad;
  assert(!Config::Load(Write("malformed", "{ not json"), bad, err));
  std::printf("refuse:   malformed file -> %s\n", err.c_str());

  bad = Config{};
  assert(!Config::Load(Write("notobj", "[1,2,3]"), bad, err));
  std::printf("refuse:   non-object top level -> %s\n", err.c_str());

  // A negative timeout is a typo. Treating it as "disabled" would silently
  // remove the only thing stopping a stuck PTT.
  bad = Config{};
  assert(!Config::Load(Write("negppt", R"({"ptt_timeout_seconds": -5})"), bad, err));
  assert(bad.ptt_timeout_seconds == 180);   // a rejected file must change NOTHING
  std::printf("refuse:   negative ptt_timeout_seconds -> %s\n", err.c_str());

  // Zero IS legitimate - explicitly disabled.
  Config zero;
  assert(Config::Load(Write("zeroppt", R"({"ptt_timeout_seconds": 0})"), zero, err));
  assert(zero.ptt_timeout_seconds == 0);
  std::printf("allow:    zero disables the watchdog deliberately\n");

  // A user that cannot authenticate is a mistake, not a disabled account.
  bad = Config{};
  assert(!Config::Load(Write("nouserhash", R"({"web_users":[{"username":"joe"}]})"), bad, err));
  std::printf("refuse:   user with no password_hash -> %s\n", err.c_str());

  bad = Config{};
  assert(!Config::Load(Write("sameport", R"({"api_port":5001,"dashboard_port":5001})"),
                       bad, err));
  assert(err.find("must differ") != std::string::npos);   // the RIGHT reason
  std::printf("refuse:   control and dashboard on one port -> %s\n", err.c_str());

  // A wrongly-typed value must not be coerced into something plausible.
  bad = Config{};
  assert(!Config::Load(Write("badtype", R"({"ptt_timeout_seconds": "soon"})"), bad, err));
  std::printf("refuse:   wrong type -> %s\n", err.c_str());

  std::printf("PASS\n");
  return 0;
}
