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

  if (cmd == "AN0;") return "AN0" + Pad(ant_, 1) + ";";
  if (cmd == "NB0;") return std::string("NB0") + (nb_ ? "1" : "0") + ";";
  if (cmd == "NR0;") return std::string("NR0") + (nr_ ? "1" : "0") + ";";
  if (cmd == "BC0;") return std::string("BC0") + (notch_ ? "1" : "0") + ";";
  if (cmd == "PA0;") return "PA0" + Pad(preamp_, 1) + ";";
  if (cmd == "RA0;") return std::string("RA0") + (att_ ? "1" : "0") + ";";
  if (cmd == "GT0;") return "GT0" + Pad(agc_, 1) + ";";
  if (cmd == "VX;")  return std::string("VX") + (vox_ ? "1" : "0") + ";";
  if (cmd == "PR0;") return std::string("PR0") + (comp_ ? "2" : "1") + ";";   // 1=OFF, 2=ON
  if (cmd == "ML0;") return std::string("ML0") + (mon_ ? "001" : "000") + ";";
  if (cmd == "RT;")  return std::string("RT") + (rit_ ? "1" : "0") + ";";
  if (cmd == "XT;")  return std::string("XT") + (xit_ ? "1" : "0") + ";";
  if (cmd == "RG0;") return "RG0" + Pad(rf_gain_, 3) + ";";
  if (cmd == "KS;")  return "KS" + Pad(cw_speed_, 3) + ";";
  if (cmd == "AG0;") return "AG0" + Pad(af_gain_, 3) + ";";
  if (cmd == "AG1;") return "AG1" + Pad(sub_af_gain_, 3) + ";";
  if (cmd == "EX010109;") return "EX010109" + Pad(ssb_out_, 3) + ";";
  if (cmd == "EX010113;") return "EX010113" + Pad(rport_gain_, 3) + ";";
  if (cmd == "EX010111;") return std::string("EX010111") + (mod_rear_ ? "1" : "0") + ";";
  if (cmd == "EX010112;") return std::string("EX010112") + (rear_usb_ ? "1" : "0") + ";";
  if (cmd == "SH0;")      return "SH00" + Pad(width_, 2) + ";";
  if (cmd == "EX030103;") return std::string("EX030103") + (rxant_ ? "1" : "0") + ";";
  if (cmd == "RT;")       return std::string("RT") + (rit_ ? "1" : "0") + ";";

  // Meters. Constant, and deliberately so: a simulator that invented a wandering
  // S-meter would make a dead meter path look alive.
  if (cmd == "SM0;") return "SM0000;";
  if (cmd.rfind("RM", 0) == 0 && cmd.size() == 4) return cmd.substr(0, 3) + "000;";

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
  if (cmd.rfind("FB", 0) == 0 && cmd.size() == 12) {
    freq_b_ = std::stoll(cmd.substr(2, 9));
    return true;
  }
  if (cmd.rfind("PC", 0) == 0 && cmd.size() == 6) {
    power_ = std::stoi(cmd.substr(2, 3));
    return true;
  }
  if (cmd == "TX1;") { tx_    = true;  return true; }
  if (cmd == "TX0;") { tx_    = false; return true; }
  if (cmd == "ST1;") { split_ = true;  return true; }
  if (cmd == "ST0;") { split_ = false; return true; }
  if (cmd == "VS1;") { vfo_b_ = true;  return true; }
  if (cmd == "VS0;") { vfo_b_ = false; return true; }
  if (cmd == "LK1;") { lock_  = true;  return true; }
  if (cmd == "LK0;") { lock_  = false; return true; }

  // Flags: <verb><0|1>;  The verb carries its own sub-index where the rig uses
  // one (NB0, NR0, ...), so the table in api.cpp writes the exact CAT string.
  auto flag = [&](const char* pfx, bool& target) {
    const std::string on  = std::string(pfx) + "1;";
    const std::string off = std::string(pfx) + "0;";
    if (cmd == on)  { target = true;  return 1; }
    if (cmd == off) { target = false; return 1; }
    return 0;
  };
  if (flag("NB0", nb_))    return true;
  if (flag("NR0", nr_))    return true;

  if (flag("RA0", att_))   return true;
  if (flag("VX",  vox_))   return true;
  if (cmd == "PR02;") { comp_ = true;  return true; }
  if (cmd == "PR01;") { comp_ = false; return true; }

  if (flag("RT",  rit_))   return true;
  if (flag("XT",  xit_))   return true;

  if (cmd == "BC01;") { notch_ = true;  return true; }
  if (cmd == "BC00;") { notch_ = false; return true; }
  if (cmd == "ML0001;") { mon_ = true;  return true; }
  if (cmd == "ML0000;") { mon_ = false; return true; }
  if (cmd == "EX0301031;") { rxant_ = true;  return true; }
  if (cmd == "EX0301030;") { rxant_ = false; return true; }
  if (cmd.rfind("RU", 0) == 0 && cmd.size() == 7) { rit_offset_ =  std::stoi(cmd.substr(2, 4)); return true; }
  if (cmd.rfind("RD", 0) == 0 && cmd.size() == 7) { rit_offset_ = -std::stoi(cmd.substr(2, 4)); return true; }
  if (cmd.rfind("SH00", 0) == 0 && cmd.size() == 7) { width_ = std::stoi(cmd.substr(4, 2)); return true; }
  if (cmd == "SV;") { std::swap(freq_a_, freq_b_); return true; }
  if (cmd.rfind("AG0", 0) == 0 && cmd.size() == 7) { af_gain_     = std::stoi(cmd.substr(3, 3)); return true; }
  if (cmd.rfind("AG1", 0) == 0 && cmd.size() == 7) { sub_af_gain_ = std::stoi(cmd.substr(3, 3)); return true; }
  if (cmd.rfind("EX010113", 0) == 0 && cmd.size() == 12) { rport_gain_ = std::stoi(cmd.substr(8, 3)); return true; }
  if (cmd == "EX0101111;") { mod_rear_ = true;  return true; }
  if (cmd == "EX0101110;") { mod_rear_ = false; return true; }
  if (cmd == "EX0101121;") { rear_usb_ = true;  return true; }
  if (cmd == "EX0101120;") { rear_usb_ = false; return true; }
  if (cmd.rfind("PA0", 0) == 0 && cmd.size() == 5) { preamp_ = cmd[3] - '0'; return true; }
  if (cmd.rfind("GT0", 0) == 0 && cmd.size() == 5) { agc_    = cmd[3] - '0'; return true; }
  if (cmd.rfind("AN0", 0) == 0 && cmd.size() == 5) { ant_    = cmd[3] - '0'; return true; }
  if (cmd.rfind("KS",  0) == 0 && cmd.size() == 6) { cw_speed_ = std::stoi(cmd.substr(2, 3)); return true; }

  if (cmd.rfind("RG0", 0) == 0 && cmd.size() == 6) { rf_gain_  = std::stoi(cmd.substr(3, 3)); return true; }
  if (cmd == "RC;")  { rit_offset_ = 0; return true; }

  // An unrecognised set-command must be REFUSED, not silently swallowed. A
  // simulator that accepts anything makes malformed CAT look like working CAT,
  // and the bug then only appears with the radio attached - the most expensive
  // place to find it.
  return false;
}
