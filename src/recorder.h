#pragma once

// Recording, and the replay buffer.
//
// ⚠️ CARRYOVER.md section 1 is about THIS FEATURE. On the reference Linux build
// /api/record/start answers {"status":"ok","recording":true} while Start() sets
// IsRecording = false. A 200 there means the route exists, not that anything is
// recording, and the only honest signal is file_recording in the status route.
//
// So the rule here: every reported field is derived from what actually happened.
// If the file could not be opened, recording is false and the reason is said.

#include <atomic>
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

  Result Start();
  Result Stop();
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

  std::atomic<bool> recording_{false};
  std::atomic<bool> buffering_{false};
  std::atomic<long long> frames_fed_{0};
};
