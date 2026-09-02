#pragma once

// Real audio through libasound.
//
// ⚠️ NOT subprocess pipes to arecord/aplay. Driving another program's stdout is
// what the C# host does, and it is the reason latency there could only ever be
// guessed at: you cannot ask a pipe how much audio is sitting on the device.
// libasound can be asked, exactly, and that is the whole point of this port.
//
// Measured on the real station 08/30/2026:
//   capture  exact at 11025, 16000, 22050, 32000, 48000. NOT 8000 - asking for
//            8000 returns 11025 worth of data, silently.
//   playback device altsets advertise 32000, 44100, 48000 only.
//   default playback buffer measured 24048 frames at 48 kHz = 501 ms, matching
//   the ~500 ms docs/internal/CARRYOVER.md section 3 records.

#include <alsa/asoundlib.h>

#include <cstdint>
#include <string>

#include "audio.h"
#include "tx_audio.h"

// RX: the rig's receiver -> the host.
class AlsaCapture : public AudioSource {
 public:
  ~AlsaCapture() override;
  bool Open(const std::string& device, int sample_rate);

  bool Read(int16_t* out, size_t frames) override;
  int SampleRate() const override { return sample_rate_; }
  std::string Describe() const override;

  const std::string& error() const { return error_; }

 private:
  snd_pcm_t* pcm_ = nullptr;
  std::string device_;
  std::string error_;
  int sample_rate_ = 0;
  long xruns_ = 0;
};

// TX: the host -> the rig's microphone input.
class AlsaPlayback : public TxAudioSink {
 public:
  ~AlsaPlayback() override;
  bool Open(const std::string& device, int sample_rate);

  bool Write(const int16_t* samples, size_t frames) override;
  int SampleRate() const override { return sample_rate_; }
  std::string Describe() const override;

  // ⚠️ THE MEASUREMENT THE WHOLE PTT TAIL DEPENDS ON.
  // Frames currently queued on the device, from the kernel. Returns -1 if it
  // cannot be read.
  //
  // Do NOT infer this from bytes written minus time elapsed. In steady state
  // that is always ~0, because the sender sends at real time - it reports
  // "nothing queued" while half a second sits in ALSA, and an estimate whose
  // failure mode is zero looks exactly like a working measurement
  // (docs/internal/CARRYOVER.md section 3). That bug cut the end off every transmission.
  long QueuedFrames() const;
  int QueuedMs() const override;
  long Xruns() const override { return xruns_; }

 private:
  snd_pcm_t* pcm_ = nullptr;
  std::string device_;
  std::string error_;
  int sample_rate_ = 0;
  long xruns_ = 0;
};

// Cross-check for QueuedFrames(), read from /proc/asound rather than the API.
// Two independent sources for the number the transmit tail depends on.
long ProcAsoundDelayFrames(const std::string& card_hint);
