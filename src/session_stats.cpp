#include "session_stats.h"

#include <cstdio>

namespace {
// The HF/6 m allocations this rig covers. A frequency outside them gets an
// empty band rather than the nearest one - "60m" printed for a shortwave
// broadcast listen is a wrong fact, and an empty string is not.
struct BandRange { long long lo, hi; const char* name; };
constexpr BandRange kBands[] = {
    {  1800000,   2000000, "160m"},
    {  3500000,   4000000,  "80m"},
    {  5330000,   5410000,  "60m"},
    {  7000000,   7300000,  "40m"},
    { 10100000,  10150000,  "30m"},
    { 14000000,  14350000,  "20m"},
    { 18068000,  18168000,  "17m"},
    { 21000000,  21450000,  "15m"},
    { 24890000,  24990000,  "12m"},
    { 28000000,  29700000,  "10m"},
    { 50000000,  54000000,   "6m"},
};

// A move counts once the dial has settled and is at least this far from the
// last counted spot. Below it, the operator is tuning around a signal, not
// going somewhere else.
constexpr long long kQsyThresholdHz = 1000;
}  // namespace

std::string SessionStats::BandFor(long long freq_hz) {
  for (const auto& b : kBands)
    if (freq_hz >= b.lo && freq_hz <= b.hi) return b.name;
  return "";
}

std::string SessionStats::Hms(long long seconds) {
  if (seconds < 0) seconds = 0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", seconds / 3600,
                (seconds / 60) % 60, seconds % 60);
  return buf;
}

void SessionStats::Observe(bool connected, long long freq_hz,
                           const std::string& mode, bool tx) {
  std::lock_guard<std::mutex> lock(mu_);

  // ⚠️ A disconnected rig reports nothing, and nothing is not zero. Treating a
  // dropped poll as "frequency 0, not transmitting" would close a running
  // transmission and count a QSY back down to 0 Hz on every serial hiccup.
  if (!connected) {
    prev_tick_freq_ = 0;
    return;
  }

  // --- keyed time, edge-detected on what the RIG says ----------------------
  if (tx && !last_tx_) {
    ++tx_count_;
    tx_started_ = Clock::now();
  } else if (!tx && last_tx_) {
    tx_time_ += std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - tx_started_);
  }
  last_tx_ = tx;

  if (freq_hz <= 0) return;

  // --- QSY, on a settled frequency -----------------------------------------
  if (freq_hz == prev_tick_freq_) {
    if (last_qsy_freq_ == 0) {
      last_qsy_freq_ = freq_hz;               // baseline: the first spot is not a move
    } else if (std::abs(freq_hz - last_qsy_freq_) >= kQsyThresholdHz) {
      ++qsy_count_;
      last_qsy_freq_ = freq_hz;
    }

    // ⚠️ Band is recorded on any SETTLED frequency, including the first one of
    // the session - the band you start on is a band you were on. It is gated on
    // settling, not on the QSY count, so a sweep through 40 m on the way to
    // 20 m still does not register as a visit to 40 m.
    const std::string band = BandFor(freq_hz);
    if (!band.empty() && band != last_band_) {
      ++band_changes_[band];
      last_band_ = band;
    }
  }
  prev_tick_freq_ = freq_hz;

  if (!mode.empty() && mode != last_mode_) {
    ++mode_changes_[mode];
    last_mode_ = mode;
  }
}

void SessionStats::CountRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  ++recordings_;
}

SessionStats::Snapshot SessionStats::Get() const {
  std::lock_guard<std::mutex> lock(mu_);
  Snapshot s;
  s.session_seconds = std::chrono::duration_cast<std::chrono::seconds>(
      Clock::now() - start_).count();
  s.qsy_count = qsy_count_;
  s.tx_count = tx_count_;
  s.recordings = recordings_;

  // ⚠️ Include the transmission IN PROGRESS. A panel that shows TX time frozen
  // while the operator is holding a long-winded over reads as a broken counter,
  // and the number it shows is wrong for as long as the key is down.
  auto total = tx_time_;
  if (last_tx_) {
    total += std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - tx_started_);
  }
  s.tx_seconds = total.count() / 1000;
  s.band_changes = band_changes_;
  s.mode_changes = mode_changes_;
  return s;
}

void SessionStats::Reset() {
  std::lock_guard<std::mutex> lock(mu_);
  start_ = Clock::now();
  qsy_count_ = tx_count_ = recordings_ = 0;
  tx_time_ = std::chrono::milliseconds{0};
  prev_tick_freq_ = last_qsy_freq_ = 0;
  // ⚠️ last_tx_ is deliberately NOT cleared. If the rig is keyed right now,
  // forgetting that loses the unkey edge and the next unkey adds time measured
  // from an unset start.
  if (last_tx_) tx_started_ = Clock::now();
  last_band_.clear();
  last_mode_.clear();
  band_changes_.clear();
  mode_changes_.clear();
}
