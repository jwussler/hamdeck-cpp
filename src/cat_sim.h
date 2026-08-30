#pragma once

// Simulated FTDX-101MP.
//
// Answers the subset of the CAT set that /api/status, /api/status/full and
// /api/meters are built from, out of in-memory state that set-commands mutate.
// Command and reply formats are the real ones from the FTDX-101 CAT reference,
// so code written against this transport works unchanged against the radio.
//
// It deliberately does NOT simulate: audio (there is no audio over CAT), amp or
// TGXL tuning (those are separate hardware), or timing. Anything about latency,
// buffer depth or PTT tail MUST be measured on the real station - CARRYOVER.md
// section 3 is explicit that estimates whose failure mode is zero look exactly
// like working measurements.

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "cat.h"

class SimulatedRig : public CatTransport {
 public:
  std::optional<std::string> Exchange(const std::string& cmd) override;
  bool Send(const std::string& cmd) override;
  bool Connected() const override { return true; }
  std::string Describe() const override { return "simulated FTDX-101MP"; }

 private:
  mutable std::mutex mu_;

  // Powered-on defaults chosen to look nothing like the live station, so a
  // simulator reading is never mistaken for a real one in a screenshot or log.
  long long freq_a_ = 14074000;  // 20m FT8 - deliberately not what the station runs
  long long freq_b_ = 14074000;
  int  mode_code_   = 2;         // 2 = USB in the FTDX-101 MD table
  int  power_       = 5;         // a real station is not running 5 W
  bool tx_          = false;
  bool split_       = false;
  bool vfo_b_       = false;
  bool lock_        = false;

  // /api/status/full and /api/meters state. Values chosen to be plainly
  // synthetic rather than a believable station.
  int  ant_    = 1;
  bool nb_     = false;
  bool nr_     = false;
  bool notch_  = false;
  int  preamp_ = 0;
  bool att_    = false;
  int  agc_    = 4;      // AUTO
  bool vox_    = false;
  bool comp_   = false;
  bool mon_    = false;
  bool rit_    = false;
  bool xit_    = false;
  int  rf_gain_ = 128;
  int  cw_speed_ = 20;
  int  width_   = 10;
  int  rit_offset_ = 0;
  bool rxant_ = false;
};
