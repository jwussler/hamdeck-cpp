#include "qso_record.h"

#include "recorder.h"

QsoRecorder::QsoRecorder(Recorder* rec, Options opts,
                         std::function<Clock::time_point()> now)
    : rec_(rec), opts_(opts), now_(std::move(now)) {}

void QsoRecorder::Observe(bool connected, long long freq_hz,
                          const std::string& mode, bool tx, bool tuning) {
  if (!rec_) return;

  // The poll loop is the only place that knows all of this at once, so it is
  // also where the sidecar's frequency comes from - fed always, so a MANUAL
  // recording gets provenance too.
  rec_->UpdateProvenance(connected, freq_hz, mode);

  const bool rising = tx && !last_tx_;
  const bool falling = !tx && last_tx_;
  // ⚠️ A tune does not move the remembered PTT state at all (the C# does the
  // same at MainWindow.xaml.cs:438). Letting it would mean the tune's unkey
  // registers as the end of an over the operator never started.
  if (!tuning) last_tx_ = tx;

  if (opts_.enabled && rising && !tuning) {
    if (!active_) {
      // ⚠️ active_ is set from whether the file OPENED, never from having
      // decided to record - the same rule the Recorder itself follows. A full
      // disk must not leave a state machine believing it is recording.
      const auto r = rec_->Start("qso");
      if (!r.ok) return;
      active_ = true;
      start_freq_ = freq_hz;
    }
    deadline_ = now_() + std::chrono::seconds(opts_.idle_seconds);
  }

  // ⚠️ AFTER the start, not before. Noting the over first drops the very over
  // that began the recording: NoteOver does nothing when no file is open, so
  // the first transmission of every QSO went unlisted while every later one
  // was recorded. Caught by the test asserting two overs, not by reading it.
  if (!tuning && (rising || falling)) rec_->NoteOver(tx);

  if (!active_) return;

  if (now_() > deadline_) {
    Stop("idle");
    return;
  }
  // ⚠️ QSY is measured from where the QSO STARTED, not from the last reading.
  // Tuning across the band in small steps would never trip a
  // reading-to-reading comparison, and the recording would run until the idle
  // timer caught it - filed under a frequency the operator left long ago.
  if (start_freq_ > 0 && connected) {
    const long long moved = freq_hz > start_freq_ ? freq_hz - start_freq_
                                                  : start_freq_ - freq_hz;
    if (moved > opts_.qsy_threshold_hz) Stop("qsy");
  }
}

void QsoRecorder::Stop(const std::string& reason) {
  // Stop() writes the sidecar with this reason in it, so a recording says how
  // it ended rather than only when.
  rec_->Stop(reason);
  active_ = false;
  start_freq_ = 0;
  last_stop_ = reason;
  ++stopped_;
}
