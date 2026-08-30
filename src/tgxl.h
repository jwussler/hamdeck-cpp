#pragma once

// TGXL — the external antenna tuner, reached over TCP.
//
// ⚠️ THIS IS THE RIGHT TUNER FOR THIS STATION. CARRYOVER.md section 2 is
// explicit: /api/tune is the rig's INTERNAL ATU and is the wrong one. They are
// kept separate and each names itself in its reply, so a confirmation can never
// say just "tuning" and leave the operator guessing which box is about to key up.
//
// Protocol, from the reference implementation:
//   connect TCP to <host>:<port>, 3 s connect timeout
//   send    "C1|autotune\n"
//   poll    "C1|status\n" and read lines containing "tuning=<0|1>"
//   done    when tuning goes 1 -> 0
//
// ⚠️ EARLY TRANSITIONS ARE IGNORED FOR THE FIRST 2 SECONDS. The tuner reports
// tuning=0 briefly before it starts, so a naive "wait for 0" returns instantly
// and reports a tune that never happened.

#include <atomic>
#include <mutex>
#include <string>

class TgxlTuner {
 public:
  TgxlTuner(std::string host, int port) : host_(std::move(host)), port_(port) {}

  bool configured() const { return !host_.empty(); }
  bool IsActive() const { return active_.load(); }

  struct Result {
    bool ok = false;
    bool tuning = false;
    std::string action;    // "started" | "stopped" | "unavailable"
    std::string message;
  };

  // Starts a tune and blocks until the tuner reports it finished, or the overall
  // limit expires. Safe to call from a request thread: it touches the network,
  // not the serial port.
  Result Tune();

  std::string Describe() const;

  static constexpr int kConnectTimeoutMs = 3000;
  static constexpr int kReadTimeoutMs    = 2000;
  // ⚠️ A hard overall limit. A tuner that never reports finished must not hold a
  // request - or an operator - for ever.
  static constexpr int kOverallLimitMs   = 45000;
  static constexpr int kIgnoreEarlyMs    = 2000;

 private:
  std::string host_;
  int port_;
  std::atomic<bool> active_{false};
  std::mutex mu_;
};
