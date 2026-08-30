#pragma once

// Cached rig state, plus the poller that keeps it fresh.
//
// ⚠️ THE POINT OF THIS FILE. /api/status is served ENTIRELY from this cache and
// never touches the serial port from a request thread - the serial lock is not
// re-entrant across threads (CARRYOVER.md section 5). The C# Linux host shipped
// with no poller at all, so /api/status served a frequency 3.6 HOURS stale and a
// tx:true left over from a tune while the rig was receiving. A cache with nothing
// refreshing it is worse than no cache: it answers confidently and wrongly.
//
// Hence cache_age_ms and stale are part of the contract, not diagnostics. A
// caller must always be able to tell a fresh answer from a stuck one.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cat.h"

struct RigSnapshot {
  bool        connected = false;
  long long   freq      = 0;
  std::string mode      = "";
  std::string vfo       = "A";
  int         power     = 0;
  bool        tx        = false;
  bool        split     = false;
  bool        vfo_locked = false;
  long long   freq_b    = 0;
  std::chrono::steady_clock::time_point taken{};
};

class RadioPoller {
 public:
  explicit RadioPoller(std::unique_ptr<CatTransport> cat);
  ~RadioPoller();

  void Start();
  void Stop();

  RigSnapshot Snapshot() const;
  long long   CacheAgeMs() const;

  // Anything older than this is reported stale. Set well above the poll interval
  // so a single slow round trip is not an alarm, but low enough that a wedged
  // poller is obvious within a couple of seconds.
  static constexpr long long kStaleAfterMs = 1500;
  static constexpr int       kPollIntervalMs = 200;  // matches the WPF host

  std::string Backend() const { return cat_->Describe(); }

 private:
  void PollLoop();
  void PollOnce();

  std::unique_ptr<CatTransport> cat_;
  mutable std::mutex mu_;
  RigSnapshot snap_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// FTDX-101 MD table. Returns "" for a code we do not know, never a guess - a
// wrong mode on the panel is worse than a blank one.
std::string ModeName(int code);
