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
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cat.h"
#include "session_stats.h"

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

  // /api/status/full. Polled on a SLOWER cadence than the core fields - see
  // kFullEveryNCycles. None of these move fast enough to be worth a serial round
  // trip five times a second, and the serial port is the scarce resource.
  int         ant       = 1;
  bool        rxant     = false;
  bool        nb        = false;
  bool        nr        = false;
  bool        notch     = false;
  int         preamp    = 0;
  bool        att       = false;
  std::string agc       = "AUTO";
  bool        vox       = false;
  bool        comp      = false;
  bool        mon       = false;
  bool        rit       = false;
  int         rit_offset = 0;
  bool        xit       = false;
  int         rf_gain   = 0;
  int         cw_speed  = 0;
  int         af_gain   = 0;
  int         sub_af_gain = 0;
  int         width_idx = 0;

  // /api/meters. These DO move fast, so they ride the fast loop.
  int         s_meter   = 0;
  int         swr       = 0;
  int         alc       = 0;
  int         power_mtr = 0;

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

  // ⚠️ THE ONLY WAY A REQUEST THREAD MAY TOUCH THE RADIO.
  // The serial lock is not re-entrant across threads (CARRYOVER.md section 5),
  // so exactly one thread - the poller - ever speaks to the port. Handlers queue
  // a command and return; the poller drains the queue at the top of each cycle.
  // Queueing also gives commands a natural ordering, which matters for pairs
  // like "set VFO then set frequency" that are wrong if they interleave.
  void Enqueue(const std::string& cat_command);

  // A compound read-modify-write, run ON THE POLLER THREAD with direct CAT
  // access. Some operations are not one verb: quick-split reads the frequency,
  // selects VFO B, writes freq+offset, selects A and sets split. Doing that from
  // a request thread would need the serial port from two threads, which the lock
  // does not allow; doing it as separate queued commands would let a poll read
  // land in the middle and cache a half-applied state.
  void EnqueueTask(std::function<void(CatTransport&)> task);

  // Transmit watchdog. Zero disables it.
  //
  // ⚠️ THIS MUST LIVE NEXT TO THE RADIO (CARRYOVER.md section 4b). A timeout in
  // the client or the browser protects nothing: close the tab, sleep the laptop
  // or lose the link while keyed and the rig stays keyed with nobody watching.
  // The Linux host shipped without this for months because it existed only in
  // the WPF host's window class.
  // Optional. When set, every poll cycle feeds it what the rig reports, so
  // the counts follow the RADIO rather than any one client.
  void SetSessionStats(SessionStats* stats) { stats_ = stats; }

  void SetPttTimeoutSeconds(int seconds) { ptt_timeout_s_.store(seconds); }
  int  PttTimeoutSeconds() const { return ptt_timeout_s_.load(); }

  // Fired when the watchdog drops PTT, with the seconds held. For logging.
  void OnWatchdogTrip(std::function<void(double)> cb) { watchdog_cb_ = std::move(cb); }
  int  WatchdogTrips() const { return watchdog_trips_.load(); }

  // Seconds before the watchdog drops PTT. 0 when receiving or disabled.
  // A client counts this down instead of inventing its own timeout - which is
  // the whole point of the watchdog living next to the radio.
  int TransmitSecondsRemaining() const;

  // ⚠️ DROP PTT AND WAIT FOR THE RIG TO CONFIRM. Called on shutdown.
  //
  // The transmit watchdog lives in THIS PROCESS. If the process exits while the
  // rig is keyed, the watchdog dies with it and nothing on earth drops PTT - the
  // station sits there with an open carrier until somebody walks up to it. That
  // is worse than the stuck-PTT case the watchdog was written for, because there
  // is no longer anything watching at all.
  //
  // Returns true if the rig confirmed it stopped transmitting.
  bool UnkeyAndConfirm(int timeout_ms = 1500);

  // Default from the C# Config: ptt_timeout_seconds = 180.
  static constexpr int kDefaultPttTimeoutSeconds = 180;

  // Anything older than this is reported stale. Set well above the poll interval
  // so a single slow round trip is not an alarm, but low enough that a wedged
  // poller is obvious within a couple of seconds.
  static constexpr long long kStaleAfterMs = 1500;
  static constexpr int       kPollIntervalMs = 200;  // matches the WPF host
  // The full set is read every fifth cycle (~1s). Reading ~16 extra CAT commands
  // at 200ms would spend most of the serial budget on values that barely change,
  // and the serial port is single-threaded and shared with every command a
  // request thread queues.
  static constexpr int       kFullEveryNCycles = 5;

  std::string Backend() const { return cat_->Describe(); }

 private:
  void PollLoop();
  void PollOnce();
  void DrainQueue();
  void CheckWatchdog(bool tx_now);
  void PollFull(RigSnapshot& s);
  void PollMeters(RigSnapshot& s);
  int  cycle_ = 0;

  std::unique_ptr<CatTransport> cat_;
  mutable std::mutex mu_;
  RigSnapshot snap_;
  std::atomic<bool> running_{false};
  std::thread thread_;

  std::atomic<bool> full_dirty_{true};
  std::mutex queue_mu_;
  std::deque<std::string> queue_;
  std::deque<std::function<void(CatTransport&)>> tasks_;

  std::atomic<int> ptt_timeout_s_{kDefaultPttTimeoutSeconds};
  SessionStats* stats_ = nullptr;
  std::atomic<int> watchdog_trips_{0};
  std::function<void(double)> watchdog_cb_;
  std::chrono::steady_clock::time_point keyed_since_{};
  bool was_tx_ = false;
};

// FTDX-101 MD table. Returns "" for a code we do not know, never a guess - a
// wrong mode on the panel is worse than a blank one.
std::string ModeName(int code);

// The inverse. ⚠️ Lives here rather than in api.cpp because the TUNER needs it
// too: it sets CW to tune and puts the operator's mode back afterwards. Two
// copies of a protocol table is two things to get wrong, and the failure is a
// rig left in the wrong mode.
int ModeCode(const std::string& name);
