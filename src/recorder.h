#pragma once

// Recording, and the replay buffer.
//
// ⚠️ docs/internal/CARRYOVER.md section 1 is about THIS FEATURE. On the reference Linux build
// /api/record/start answers {"status":"ok","recording":true} while Start() sets
// IsRecording = false. A 200 there means the route exists, not that anything is
// recording, and the only honest signal is file_recording in the status route.
//
// So the rule here: every reported field is derived from what actually happened.
// If the file could not be opened, recording is false and the reason is said.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class Recorder {
 public:
  Recorder(std::string dir, int sample_rate, int buffer_seconds,
           int max_seconds, int warn_seconds);
  ~Recorder();

  // Fed from the RX stream. Always fills the ring; writes to file only while
  // recording.
  void Feed(const int16_t* samples, size_t frames);

  struct Result {
    bool ok = false;
    std::string filename;
    std::string message;
  };

  // ⚠️ WHAT THE RADIO WAS DOING IS PART OF THE RECORDING. A .wav on its own
  // cannot be matched to a log: the filename is local time by house style, and
  // every log worth matching against (Wavelog QSOs, NetLogger check-ins) is UTC.
  // So each file gets a .json sidecar carrying UTC start/end, frequency, mode
  // and the operator's overs. Set by main.cpp from the rig poller; without it
  // the sidecar still gets written, saying it did not know rather than guessing.
  struct Provenance {
    bool connected = false;
    long long freq_hz = 0;
    std::string mode;
  };
  void UpdateProvenance(bool connected, long long freq_hz, const std::string& mode);

  // An "over" is one transmission. Fed from the same PTT edges the auto-record
  // watches, so a long recording can be navigated by who was talking when -
  // in a net recording the operator's overs are what bracket each exchange.
  void NoteOver(bool keyed);

  Result Start(const std::string& tag = "rec");
  // The trigger is recorded in the sidecar: what stopped this recording is the
  // difference between a QSO that ended and a disk limit that cut one off.
  Result Stop(const std::string& trigger = "manual");
  // ⚠️ Writes the ring buffer - the audio from BEFORE the operator pressed
  // anything. That is the entire point: you press it after hearing something,
  // not before.
  Result SaveReplay();

  bool recording() const { return recording_.load(); }
  bool buffering() const { return buffering_.load(); }
  bool available() const { return available_; }
  std::string directory() const { return dir_; }
  long long frames_fed() const { return frames_fed_.load(); }
  int recorded_seconds() const;
  std::string unavailable_reason() const { return reason_; }

 private:
  bool WriteWavHeader(std::FILE* f, uint32_t data_bytes) const;
  Result OpenFile(const std::string& tag);
  void CloseFile();
  Provenance Ask() const;              // takes prov_mu_, never mu_
  // Assumes mu_ is held.
  struct Over { std::chrono::system_clock::time_point start, end; bool open = false; };
  // ⚠️ overs == nullptr means NOT TRACKED, and the sidecar says null rather than
  // [] - a replay clip has no over list, and an empty array would claim the
  // operator never transmitted during it. Those are different facts.
  void WriteSidecar(const std::string& wav_path, const std::string& trigger,
                    std::chrono::system_clock::time_point started,
                    const Provenance& at_start,
                    const std::vector<Over>* overs) const;

  std::string dir_;
  std::string reason_;
  int rate_;
  size_t ring_max_frames_;
  int max_seconds_;
  int warn_seconds_;
  bool available_ = false;

  mutable std::mutex mu_;
  std::deque<int16_t> ring_;
  std::FILE* file_ = nullptr;
  std::string file_path_;
  uint32_t file_frames_ = 0;

  // Its own lock, held only for the copy - never while mu_ is being taken.
  mutable std::mutex prov_mu_;
  Provenance prov_;
  std::chrono::system_clock::time_point file_started_{};
  Provenance file_start_prov_;
  // Each entry is one over: UTC start, and UTC end once it is unkeyed.
  std::vector<Over> overs_;

  std::atomic<bool> recording_{false};
  std::atomic<bool> buffering_{false};
  std::atomic<long long> frames_fed_{0};
};
