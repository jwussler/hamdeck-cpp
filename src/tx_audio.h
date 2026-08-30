#pragma once

// TX audio: client microphone -> host -> the rig's USB codec.
//
// ⚠️ THIS PATH PUTS A HUMAN VOICE ON THE AIR. Every rule here is about not
// transmitting the wrong thing, or transmitting nothing while appearing to work.
//
// Wire format is 48000 Hz / 16-bit / mono (CARRYOVER.md section 2). RX is 22050
// and TX is 48000 because the codec's capture supports 8000-48000 but its
// PLAYBACK only supports 32000-48000 - the asymmetry is the device's, not a
// choice.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Where TX audio goes. The real implementation writes the codec through
// libasound; until the radio is on this machine, the counting sink stands in.
class TxAudioSink {
 public:
  virtual ~TxAudioSink() = default;
  virtual bool Write(const int16_t* samples, size_t frames) = 0;
  virtual int SampleRate() const = 0;
  virtual std::string Describe() const = 0;
};

// Counts and discards. Reports honestly that it is not a radio.
class NullTxSink : public TxAudioSink {
 public:
  explicit NullTxSink(int sample_rate = 48000) : sample_rate_(sample_rate) {}
  bool Write(const int16_t*, size_t frames) override {
    frames_.fetch_add(frames);
    return true;
  }
  int SampleRate() const override { return sample_rate_; }
  std::string Describe() const override { return "null sink (discards, no radio)"; }
  size_t FramesWritten() const { return frames_.load(); }

 private:
  int sample_rate_;
  std::atomic<size_t> frames_{0};
};

class TxAudioReceiver {
 public:
  explicit TxAudioReceiver(std::unique_ptr<TxAudioSink> sink);

  // ⚠️ MUST join the pump thread. Destroying a std::thread that is still
  // joinable calls std::terminate, so without this the host ABORTS instead of
  // exiting cleanly on any early-return path - which is exactly what a failed
  // port bind is. It turned a clean "failed to bind, exit 1" into SIGABRT.
  ~TxAudioReceiver();

  // ⚠️ EXACTLY ONE transmitting client at a time. Two clients feeding the
  // transmitter would interleave two voices into one carrier. Claim() fails if
  // somebody already holds it - it does not queue, and it does not evict.
  bool Claim(const std::string& who);
  void Release(const std::string& who);
  bool HeldBy(const std::string& who) const;
  std::string Holder() const;

  // Accepts one frame of PCM. `keyed` is the rig's own TX state.
  bool Accept(const char* data, size_t bytes, bool keyed);

  size_t Accepted() const { return accepted_.load(); }
  size_t Dropped() const { return dropped_.load(); }
  size_t QueueDepth() const;
  std::string Backend() const { return sink_->Describe(); }
  int SampleRate() const { return sink_->SampleRate(); }

  // Drains queued audio to the sink. Called by the playback loop; separate so it
  // can be driven deterministically in tests.
  size_t Pump(size_t max_chunks = 64);

  // Runs Pump on its own thread. Without this the queue only ever grows: audio
  // arrives from the client and nothing moves it to the sink, so /ws/tx would
  // accept frames, report success, and transmit nothing - a working-looking path
  // that is not connected to anything.
  void Start();
  void Stop();

  // ~500 ms at 48k in 20 ms chunks.
  static constexpr size_t kMaxQueuedChunks = 25;

 private:
  void PumpLoop();

  std::unique_ptr<TxAudioSink> sink_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mu_;
  std::deque<std::vector<int16_t>> queue_;
  std::string holder_;
  std::atomic<size_t> accepted_{0}, dropped_{0};
};
