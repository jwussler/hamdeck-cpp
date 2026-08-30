#pragma once

// What this operating session has actually done: how long, how many moves, how
// much time with the transmitter keyed.
//
// ⚠️ THIS LIVES ON THE HOST, not in a client. In the C# app the counting ran in
// the WPF window's poll loop, so the numbers belonged to whichever client
// happened to be open - a second client saw zero, and closing the window reset
// them. The rig is the shared thing, so the count of what the rig did is too.
//
// Everything here is derived from OBSERVED rig state, never from a route having
// been called. Keying by the microphone button counts the same as keying from
// the panel, because both are seen the same way: the rig says it is keyed.

#include <chrono>
#include <map>
#include <mutex>
#include <string>

class SessionStats {
 public:
  SessionStats() = default;

  // Fed once per poll cycle with what the rig reports.
  void Observe(bool connected, long long freq_hz, const std::string& mode,
               bool tx);

  // Counted when a recording finishes - which is what the reference called a
  // "QSO count". It is not a logbook entry and is not claimed to be one.
  void CountRecording();

  struct Snapshot {
    long long session_seconds = 0;
    int qsy_count = 0;
    int tx_count = 0;
    long long tx_seconds = 0;
    int recordings = 0;
    std::map<std::string, int> band_changes;
    std::map<std::string, int> mode_changes;
  };
  Snapshot Get() const;
  void Reset();

  // hh:mm:ss, the format the panel shows.
  static std::string Hms(long long seconds);
  // "20m", or "" outside the ham bands - never a guess.
  static std::string BandFor(long long freq_hz);

 private:
  using Clock = std::chrono::steady_clock;

  mutable std::mutex mu_;
  Clock::time_point start_ = Clock::now();

  int qsy_count_ = 0;
  int tx_count_ = 0;
  int recordings_ = 0;
  std::chrono::milliseconds tx_time_{0};

  // ⚠️ A QSY is a SETTLED move. Counting every frequency the dial passes turns
  // one spin across the band into a hundred QSYs, which is why this waits for
  // the frequency to be the same two cycles running before it counts.
  long long prev_tick_freq_ = 0;
  long long last_qsy_freq_ = 0;

  bool last_tx_ = false;
  Clock::time_point tx_started_;

  std::string last_band_;
  std::string last_mode_;
  std::map<std::string, int> band_changes_;
  std::map<std::string, int> mode_changes_;
};
