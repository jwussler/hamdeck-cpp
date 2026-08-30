#pragma once

// The amplifier tune: a timed carrier and nothing else.
//
// No external tuner is involved. The amplifier listens for RF and tunes its own
// network, so all this does is put a clean, low-power carrier on the air for
// long enough for it to finish. Ported from Services/Tuners.cs (AmpTuner):
//
//   1  save the current power and mode
//   2  set 20 W, set CW
//   3  KEY for 10 seconds
//   4  UNKEY
//   5  set 100 W  - the TARGET operating power, deliberately NOT the saved one
//   6  restore the saved mode
//
// ⚠️ STEP 5 IS NOT A BUG AND MUST NOT BE "FIXED". The reference host ends the
// sequence at 100 W rather than putting the old power back, because the point
// of tuning the amplifier is to then operate through it. Restoring 5 W after
// tuning an amplifier is not what anybody pressed the button for.
//
// ⚠️ LOCAL CALLERS ONLY, enforced by the route, not here: this keys the
// transmitter for ten unattended seconds. "Local" means the request arrived on
// the loopback listener - a kernel guarantee, not a header a caller can set.
//
// ⚠️ Ten seconds is a long carrier. Every exit path unkeys: the stop flag is
// checked every 100 ms, an exception unkeys and forces 100 W, and the
// destructor joins the worker so it cannot outlive the host.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "rig_control.h"

class AmpTuner {
 public:
  explicit AmpTuner(RigControl rig = {}) : rig_(std::move(rig)) {}
  ~AmpTuner();

  struct Result {
    bool ok = false;
    bool tuning = false;
    std::string action;     // "started" | "stopped"
    std::string message;
  };

  // Returns immediately; a second call while running STOPS it, as the reference
  // host does. Progress is reported through amp_tuning in /api/status.
  Result Tune();
  void Stop();
  bool IsActive() const { return active_.load(); }

  static constexpr int kTunePowerWatts  = 20;
  static constexpr int kAfterPowerWatts = 100;
  static constexpr int kCarrierMs       = 10000;
  static constexpr int kSettleMs        = 400;
  static constexpr int kPttDropMs       = 400;

 private:
  void Worker();

  RigControl rig_;
  std::atomic<bool> active_{false};
  std::atomic<bool> stop_{false};
  std::thread worker_;
  std::mutex mu_;
};
