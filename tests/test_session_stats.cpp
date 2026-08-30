// Session-stats tests.
//
// The counting rules here are the ones that are easy to get subtly wrong: a
// tuning sweep must be ONE qsy, a serial dropout must not close a transmission,
// and a transmission in progress must still be counted.

#include "../src/session_stats.h"

#include <iostream>
#include <string>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  if (!ok) ++failures;
}
// One settled observation: the poller must see the same frequency twice.
void Settle(SessionStats& s, long long hz, const std::string& mode = "USB",
            bool tx = false) {
  s.Observe(true, hz, mode, tx);
  s.Observe(true, hz, mode, tx);
}
}  // namespace

int main() {
  std::cout << "session_stats\n";

  // --- band lookup ---------------------------------------------------------
  Check(SessionStats::BandFor(14200000) == "20m", "14.200 -> 20m");
  Check(SessionStats::BandFor(7180000) == "40m", "7.180 -> 40m");
  Check(SessionStats::BandFor(1810000) == "160m", "1.810 -> 160m");
  Check(SessionStats::BandFor(50125000) == "6m", "50.125 -> 6m");
  // ⚠️ Outside the bands is EMPTY, never the nearest band.
  Check(SessionStats::BandFor(9500000).empty(), "shortwave broadcast -> no band");
  Check(SessionStats::BandFor(0).empty(), "0 Hz -> no band");

  Check(SessionStats::Hms(0) == "00:00:00", "Hms(0)");
  Check(SessionStats::Hms(3661) == "01:01:01", "Hms(3661)");
  Check(SessionStats::Hms(-5) == "00:00:00", "Hms of a negative is clamped");

  // --- the first settled frequency is a baseline, not a move ---------------
  {
    SessionStats s;
    Settle(s, 14200000);
    Check(s.Get().qsy_count == 0, "the first frequency is not a QSY");
    Check(s.Get().band_changes.at("20m") == 1, "but the band is recorded");
  }

  // --- a sweep is one QSY, not a hundred -----------------------------------
  {
    SessionStats s;
    Settle(s, 14200000);
    // Tune across 50 kHz without pausing: every tick a different frequency, so
    // nothing ever settles.
    for (int i = 1; i <= 500; ++i) s.Observe(true, 14200000 + i * 100, "USB", false);
    Check(s.Get().qsy_count == 0, "an unsettled sweep counts NOTHING");
    Settle(s, 14250000);
    Check(s.Get().qsy_count == 1, "settling at the far end counts exactly one QSY");
  }

  // --- a small move is not a QSY -------------------------------------------
  {
    SessionStats s;
    Settle(s, 14200000);
    Settle(s, 14200500);                 // 500 Hz: tuning around a signal
    Check(s.Get().qsy_count == 0, "a 500 Hz nudge is not a QSY");
    Settle(s, 14201500);                 // 1.5 kHz from the last COUNTED spot
    Check(s.Get().qsy_count == 1, "1.5 kHz from the counted spot is a QSY");
  }

  // --- band changes follow the settled QSY ---------------------------------
  {
    SessionStats s;
    Settle(s, 7180000);
    Settle(s, 14200000);
    auto g = s.Get();
    Check(g.band_changes.size() == 2 && g.band_changes.at("40m") == 1 &&
              g.band_changes.at("20m") == 1,
          "40m then 20m are both recorded once");
    Settle(s, 14250000);
    Check(s.Get().band_changes.at("20m") == 1,
          "moving WITHIN 20m does not re-count the band");
  }

  // --- mode changes --------------------------------------------------------
  {
    SessionStats s;
    Settle(s, 14200000, "USB");
    Settle(s, 14200000, "USB");
    Check(s.Get().mode_changes.at("USB") == 1, "the same mode counts once");
    Settle(s, 14200000, "CW");
    Check(s.Get().mode_changes.at("CW") == 1, "a change to CW counts");
    Check(s.Get().mode_changes.size() == 2, "and only two modes are known");
  }

  // --- keyed time, edge-detected -------------------------------------------
  {
    SessionStats s;
    s.Observe(true, 14200000, "USB", false);
    Check(s.Get().tx_count == 0, "idle: no transmissions");

    s.Observe(true, 14200000, "USB", true);
    Check(s.Get().tx_count == 1, "keying counts one transmission");
    s.Observe(true, 14200000, "USB", true);
    Check(s.Get().tx_count == 1, "staying keyed does not count again");

    // ⚠️ The in-progress transmission must already be in tx_seconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Check(s.Get().tx_seconds >= 1,
          "time is counted WHILE the key is down, not only after unkey");

    s.Observe(true, 14200000, "USB", false);
    const auto after = s.Get().tx_seconds;
    Check(after >= 1, "the time survives the unkey");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Check(s.Get().tx_seconds == after, "and stops growing once unkeyed");

    s.Observe(true, 14200000, "USB", true);
    Check(s.Get().tx_count == 2, "a second over counts again");
  }

  // --- a dropped poll is not "not transmitting" ----------------------------
  {
    // ⚠️ This is the one that matters on real hardware. A serial hiccup used to
    // look like an unkey followed by a re-key, inflating tx_count on a single
    // over and splitting its time.
    SessionStats s;
    s.Observe(true, 14200000, "USB", true);
    Check(s.Get().tx_count == 1, "keyed");
    s.Observe(false, 0, "", false);            // dropped poll
    s.Observe(false, 0, "", false);
    Check(s.Get().tx_count == 1, "a dropped poll does NOT end the transmission");
    s.Observe(true, 14200000, "USB", true);
    Check(s.Get().tx_count == 1, "and does not count a second one when it returns");
  }

  // --- a dropout does not manufacture a QSY --------------------------------
  {
    SessionStats s;
    Settle(s, 14200000);
    s.Observe(false, 0, "", false);
    s.Observe(false, 0, "", false);
    Settle(s, 14200000);
    Check(s.Get().qsy_count == 0, "a dropout and return to the same spot is no QSY");
  }

  // --- recordings ----------------------------------------------------------
  {
    SessionStats s;
    Check(s.Get().recordings == 0, "no recordings to start");
    s.CountRecording();
    s.CountRecording();
    Check(s.Get().recordings == 2, "recordings are counted");
  }

  // --- reset ---------------------------------------------------------------
  {
    SessionStats s;
    Settle(s, 7180000);
    Settle(s, 14200000);
    s.Observe(true, 14200000, "USB", true);
    s.Observe(true, 14200000, "USB", false);
    Check(s.Get().qsy_count == 1 && s.Get().tx_count == 1, "counted before reset");

    s.Reset();
    auto g = s.Get();
    Check(g.qsy_count == 0 && g.tx_count == 0 && g.tx_seconds == 0,
          "reset clears the counts");
    Check(g.band_changes.empty() && g.mode_changes.empty(),
          "and the band/mode maps");
    Check(g.session_seconds < 2, "and restarts the clock");
  }

  // --- reset while KEYED ---------------------------------------------------
  {
    // ⚠️ Resetting mid-over must not lose the key-down edge; if it did, the
    // next unkey would add time measured from an unset start.
    SessionStats s;
    s.Observe(true, 14200000, "USB", true);
    s.Reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    s.Observe(true, 14200000, "USB", false);
    const auto secs = s.Get().tx_seconds;
    Check(secs >= 1 && secs < 5,
          "a reset mid-over measures from the reset, not from nowhere");
  }

  std::cout << (failures ? "FAILURES: " + std::to_string(failures)
                         : std::string("all passed (session_stats)"))
            << "\n";
  return failures ? 1 : 0;
}
