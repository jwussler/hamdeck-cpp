#pragma once

// TX audio: client microphone -> host -> the rig's USB codec.
//
// ⚠️ THIS PATH PUTS A HUMAN VOICE ON THE AIR. Every rule here is about not
// transmitting the wrong thing, or transmitting nothing while appearing to work.
//
// Wire format is 48000 Hz / 16-bit / mono (docs/internal/CARRYOVER.md section 2). RX is 22050
// and TX is 48000 because the codec's capture supports 8000-48000 but its
// PLAYBACK only supports 32000-48000 - the asymmetry is the device's, not a
// choice.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
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

  // Milliseconds currently queued ON THE DEVICE, or -1 if unknowable. This is
  // the number the adaptive buffering steers and the PTT tail waits for.
  virtual int QueuedMs() const { return -1; }
  // Underruns so far. Each one is audible - the device ran dry mid-audio.
  virtual long Xruns() const { return 0; }
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

  // ⚠️ DEAD-LINK DETECTION, and it is what makes a PHONE safe on this rig.
  //
  // The /ws/tx close handler unkeys, but it only runs when the socket actually
  // CLOSES. A phone that walks into a tunnel, loses cell, or is suspended by iOS
  // sends no FIN - the connection simply stops carrying data. From the host the
  // socket looks perfectly healthy, the close callback never fires, and the only
  // thing that ends the carrier is the 180 s transmit watchdog.
  //
  // While the operator is keyed the client streams PCM continuously, so a GAP in
  // that stream IS the link dying. There is no other signal, and it needs no
  // cooperation from the client - a phone that has stopped talking cannot answer
  // a ping either.
  //
  // ⚠️ ARMED ONLY BY THE FIRST FRAME. PTT can be keyed over HTTP with no audio
  // client at all, and a gap check armed at connect time would unkey that
  // operator instantly. Released with the claim, so the next over re-arms.
  bool LinkArmed() const { return link_armed_.load(); }
  // Milliseconds since a frame last arrived, or -1 if none ever has.
  long long MsSinceFrame() const;

  // Accepts one frame of PCM. `keyed` is the rig's own TX state.
  bool Accept(const char* data, size_t bytes, bool keyed);

  size_t Accepted() const { return accepted_.load(); }
  size_t Dropped() const { return dropped_.load(); }

  // ⚠️ RECENT peak sample of the audio actually arriving, 0-32767.
  //
  // Counting frames does not prove there is SOUND in them. A muted microphone,
  // a capsule on the silent half of a stereo pair, or a wrong capture device all
  // deliver perfectly formed silence at exactly the right rate - accepted climbs,
  // the queue behaves, the device consumes it, and the transmitter sends nothing.
  // Every counter in this class reads like success in that case. This is the one
  // number that does not.
  // ⚠️ IT MUST BE ABLE TO GO DOWN. The first version was a high-water mark that
  // only ever rose, which is useless for the job it exists for: an operator
  // turning mic gain DOWN to stop pinning ALC would watch a number that could
  // not fall. It now decays over a short window, so it reads what is arriving
  // NOW rather than the loudest thing that ever did.
  int PeakSinceReset() const;
  void ResetPeak() { peak_.store(0); }
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

  // ⚠️ THE QUEUE MUST HOLD MORE THAN THE LARGEST PRE-ROLL TARGET.
  // 50 chunks x 20 ms = 1000 ms, comfortably above the 600 ms ceiling below.
  //
  // At 25 chunks (500 ms) this DEADLOCKED on real hardware: a burst of underruns
  // grew the target to 600 ms, the pre-roll waited for 600 ms it could never
  // hold, the queue sat full at 500 ms, and audio stopped flowing entirely.
  // Found only with a real device - a null sink never underruns, so the target
  // never grows, so the deadlock never happens.
  static constexpr size_t kMaxQueuedChunks = 50;

  // ⚠️ ADAPTIVE BUFFERING (docs/internal/CARRYOVER.md section 3), and it is not optional.
  //
  // Writing each chunk to the device the moment it arrives leaves no cushion:
  // the device runs dry between chunks and underruns. Measured on the real
  // codec before this was added: **290 xruns in six seconds**, with the stream
  // sitting in XRUN. That is a stutter on every syllable, and it is invisible
  // against a null sink - only a real device shows it.
  //
  // So: give the device a generous buffer and control the FILL LEVEL. Hold audio
  // back until `target` milliseconds are queued, then feed continuously.
  static constexpr int kTargetStartMs = 150;
  static constexpr int kTargetFloorMs = 80;
  static constexpr int kTargetCeilMs  = 600;
  static constexpr int kXrunGrowMs    = 60;   // an underrun means too little cushion
  static constexpr int kDecayStepMs   = 20;   // and quiet time means we can trim
  static constexpr int kDecayAfterSec = 30;

  // 20 ms at 48 kHz, the chunk size clients send.
  static constexpr size_t kFramesPerChunkHint = 960;

  int target_ms() const { return target_ms_; }
  int DeviceQueuedMs() const { return sink_->QueuedMs(); }

 private:
  void PumpLoop();

  std::unique_ptr<TxAudioSink> sink_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mu_;
  std::deque<std::vector<int16_t>> queue_;
  std::string holder_;
  // steady_clock ms when a frame last arrived; 0 = never. Atomic because the
  // poll loop reads it every cycle while the websocket thread writes it.
  std::atomic<long long> last_frame_ms_{0};
  std::atomic<bool> link_armed_{false};
  std::atomic<size_t> accepted_{0}, dropped_{0};
  std::atomic<int> peak_{0};   // loudest sample in the current window, 0-32767
  std::atomic<long long> peak_ms_{0};   // when that window started
  static constexpr long long kPeakWindowMs = 1500;

  int  target_ms_ = kTargetStartMs;
  long last_xruns_ = 0;
  bool prerolled_ = false;
  std::chrono::steady_clock::time_point last_xrun_at_{};
};
