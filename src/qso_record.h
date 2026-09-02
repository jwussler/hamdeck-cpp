#pragma once

// PTT auto-record: a recording that brackets a QSO without being asked for.
//
// Ported from the C# panel (Views/MainWindow.xaml.cs:420) rather than invented,
// because the behaviour is the operator's habit and not a design question:
//   - the first time PTT goes down, start recording
//   - every later press pushes an idle deadline out
//   - stop when the operator has been quiet for idle_seconds, or has QSY'd
//     further than qsy_threshold_hz from where the QSO started
//
// ⚠️ IT MUST NOT START ON A TUNE. Keying an antenna tuner is PTT as far as the
// rig is concerned, and a tune at the top of every band change would litter the
// directory with two-second files that no log will ever match.
//
// ⚠️ OFF BY DEFAULT. This writes audio of whoever the operator is talking to,
// unasked. That is a decision for the operator to make once, in the config, not
// something a version bump turns on for them.

#include <chrono>
#include <functional>
#include <string>

class Recorder;

class QsoRecorder {
 public:
  using Clock = std::chrono::steady_clock;

  struct Options {
    bool enabled = false;
    int idle_seconds = 60;         // C# PTTRecordSeconds default
    long long qsy_threshold_hz = 10000;  // C# PTTQSYThresholdKHz default, 10 kHz
  };

  // The clock is injectable so the idle timeout can be tested without waiting
  // a minute for it - a test that sleeps for the real timeout gets deleted or
  // shortened until it no longer tests the thing.
  QsoRecorder(Recorder* rec, Options opts,
              std::function<Clock::time_point()> now = [] { return Clock::now(); });

  // Fed once per poll cycle, from the same place SessionStats is fed.
  void Observe(bool connected, long long freq_hz, const std::string& mode,
               bool tx, bool tuning);

  bool active() const { return active_; }
  // Why the last automatic recording stopped: "idle", "qsy", or empty if none
  // has. Reported so the operator can tell a finished QSO from a cut-off one.
  std::string last_stop_reason() const { return last_stop_; }
  int stopped_count() const { return stopped_; }

 private:
  void Stop(const std::string& reason);

  Recorder* rec_;
  Options opts_;
  std::function<Clock::time_point()> now_;

  bool active_ = false;
  bool last_tx_ = false;
  long long start_freq_ = 0;
  Clock::time_point deadline_{};
  std::string last_stop_;
  int stopped_ = 0;
};
