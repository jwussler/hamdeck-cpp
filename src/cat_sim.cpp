#include "cat_sim.h"

#include <cstdio>

namespace {

// FTDX-101 numeric fields are fixed-width, zero-padded. Getting the width wrong
// is the classic CAT bug: the rig accepts it and does something else.
std::string Pad(long long v, int width) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%0*lld", width, v);
  return buf;
}

}  // namespace

std::optional<std::string> SimulatedRig::Exchange(const std::string& cmd) {
  std::lock_guard<std::mutex> lock(mu_);

  // ID; is the ONLY safe probe. CARRYOVER.md section 9: probing with a control
  // route once changed the operating mode mid-session. 0682 is the FTDX-101MP.
  if (cmd == "ID;")  return "ID0682;";

  if (cmd == "FA;")  return "FA" + Pad(freq_a_, 9) + ";";
  if (cmd == "FB;")  return "FB" + Pad(freq_b_, 9) + ";";
  if (cmd == "MD0;") return "MD0" + Pad(mode_code_, 1) + ";";
  if (cmd == "PC;")  return "PC" + Pad(power_, 3) + ";";
  if (cmd == "TX;")  return std::string("TX") + (tx_ ? "1" : "0") + ";";
  if (cmd == "ST;")  return std::string("ST") + (split_ ? "1" : "0") + ";";
  if (cmd == "VS;")  return std::string("VS") + (vfo_b_ ? "1" : "0") + ";";
  if (cmd == "LK;")  return std::string("LK") + (lock_ ? "1" : "0") + ";";

  // An unknown command gets no reply, exactly as the radio behaves. Returning a
  // plausible default here would let a wrong command look like a working one.
  return std::nullopt;
}

bool SimulatedRig::Send(const std::string& cmd) {
  std::lock_guard<std::mutex> lock(mu_);

  if (cmd.rfind("FA", 0) == 0 && cmd.size() == 12) {
    freq_a_ = std::stoll(cmd.substr(2, 9));
    return true;
  }
  if (cmd.rfind("MD0", 0) == 0 && cmd.size() == 5) {
    mode_code_ = cmd[3] - '0';
    return true;
  }
  if (cmd == "TX1;") { tx_ = true;  return true; }
  if (cmd == "TX0;") { tx_ = false; return true; }
  return false;
}
