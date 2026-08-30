#pragma once

// TGXL — the external antenna tuner, reached over TCP.
//
// ⚠️ THIS IS THE RIGHT TUNER FOR THIS STATION. CARRYOVER.md section 2 is
// explicit: /api/tune is the rig's INTERNAL ATU and is the wrong one. They are
// kept separate and each names itself in its reply, so a confirmation can never
// say just "tuning" and leave the operator guessing which box is about to key up.
//
// ⚠️ THE TUNER NEEDS A CARRIER. THAT IS THE WHOLE SEQUENCE, AND THE FIRST PORT
// OF THIS FILE LEFT IT OUT. Sending autotune to the tuner while the transmitter
// is idle tunes nothing: the tuner has no RF to measure and the operator sees a
// button that does nothing. The reference C# host drives the RADIO as well as
// the tuner, and this now mirrors it step for step:
//
//   1  save the current power and mode
//   2  set 15 W, set CW
//   3  connect TCP to <host>:<port>, 3 s connect timeout
//   4  KEY THE TRANSMITTER, settle
//   5  send "C1|autotune\n"
//   6  poll "C1|status\n", read lines carrying "tuning=<0|1>"
//   7  UNKEY, then restore the saved power and mode
//
// ⚠️ Steps 3 and 4 are DELIBERATELY IN THIS ORDER, and the reference host has
// them the other way round. Keying first means a tuner that is switched off
// gets 15 W into the antenna for the whole 3 s connect timeout, tuning nothing.
// The tuner only needs the carrier from step 5 onwards, so connecting first
// costs nothing and an unreachable tuner now produces no RF at all.
//
// ⚠️ Steps 4 and 7 are a pair and 7 must happen on EVERY path out of this -
// timeout, refused connection, thrown exception, operator stop. A tuner that
// leaves the rig keyed at 15 W in CW is worse than one that does not tune.
//
// ⚠️ COMPLETION IS "tuning WENT 1 THEN 0", NOT "tuning IS 0". The tuner emits a
// burst of status lines the moment you connect - 0, then 1, then 0 - all inside
// a few milliseconds, and a real tune takes 3-15 seconds. So:
//   * tuning=1 arms the finish;
//   * tuning=0 finishes it only if tuning=1 was seen AND 2 s have passed;
//   * a 1->0 inside that 2 s is the burst: DISARM and wait for the real one;
//   * if tuning=1 is never seen at all, give up after 5 s.
// The first port used "seen_tuning OR elapsed > 2 s", which reports a completed
// tune at 2.001 s against a tuner that never started - the precise failure the
// comment above it claimed to prevent.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// How the tuner drives the radio. Callbacks rather than a RadioPoller pointer,
// so this file stays free of the rig and can be tested without one - and so the
// caller decides how a command reaches the serial port. ⚠️ The setters must be
// safe to call from a thread that is NOT the poller: they queue, they never
// touch the port. See RadioPoller::Enqueue.
struct TgxlRig {
  std::function<int()> get_power;
  std::function<std::string()> get_mode;
  std::function<void(int)> set_power;
  std::function<void(const std::string&)> set_mode;
  std::function<void(bool)> set_ptt;
};

class TgxlTuner {
 public:
  TgxlTuner(std::string host, int port, TgxlRig rig = {})
      : host_(std::move(host)), port_(port), rig_(std::move(rig)) {}
  ~TgxlTuner();

  bool configured() const { return !host_.empty(); }
  bool IsActive() const { return active_.load(); }

  struct Result {
    bool ok = false;
    bool tuning = false;
    std::string action;    // "started" | "stopped" | "unavailable"
    std::string message;
  };

  // ⚠️ RETURNS IMMEDIATELY, like the C# host it has to match. A tune keys the
  // transmitter for up to 45 s; holding an HTTP request open for that long
  // means the panel freezes, the operator cannot see tgxl_tuning go true, and
  // they cannot press the button again to STOP. Progress is reported through
  // /api/status's tgxl_tuning, which is what the client polls.
  //
  // ⚠️ A SECOND CALL WHILE TUNING IS A STOP, not an error - the button is a
  // toggle on the reference host, and an operator who wants the carrier to end
  // needs one press, not a support call.
  Result Tune();

  // Asks the worker to stop. Does not wait; the worker unkeys and restores.
  void Stop();

  std::string Describe() const;

  static constexpr int kConnectTimeoutMs = 3000;
  static constexpr int kReadTimeoutMs    = 2000;
  // ⚠️ A hard overall limit. A tuner that never reports finished must not hold a
  // request - or an operator - for ever.
  static constexpr int kOverallLimitMs   = 45000;
  static constexpr int kIgnoreEarlyMs    = 2000;
  // No tuning=1 within this long means the tuner never started. C# gives up here.
  static constexpr int kNoStartGiveUpMs  = 5000;
  // Tune power and mode. 15 W CW is what the reference host uses: enough RF for
  // the tuner to measure, little enough to be safe into an unmatched load.
  static constexpr int kTunePowerWatts   = 15;
  // Settle times. Longer than the C# equivalents because a command here is
  // QUEUED and goes out at the top of the poller's next 200 ms cycle, rather
  // than being written to the port there and then.
  static constexpr int kSettleMs         = 400;
  static constexpr int kPttSettleMs      = 600;
  static constexpr int kPttDropMs        = 400;

 private:
  void Worker();

  std::string host_;
  int port_;
  TgxlRig rig_;
  std::atomic<bool> active_{false};
  std::atomic<bool> stop_{false};
  std::thread worker_;
  std::mutex mu_;
};
