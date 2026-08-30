#pragma once

// RX audio fan-out.
//
// One producer thread, N WebSocket clients, ONE shared bounded queue with
// drop-oldest — the same shape as the C# AudioStreamer, which keeps a single
// send queue and trims it to 10 items before every push.
//
// ⚠️ The queue MUST be bounded and MUST drop, not block or grow. An unbounded
// queue does not avoid loss, it converts loss into ever-growing latency, and
// latency on a receive stream is indistinguishable from a broken link to the
// operator. CARRYOVER.md section 6 says it for the client side; it is just as
// true here.
//
// Which end gets dropped matters. Dropping the OLDEST keeps the stream current:
// a listener who falls behind rejoins at live audio rather than playing out an
// ever-later recording of the band.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "http.h"

// Where PCM comes from. The real implementation reads the USB codec through
// libasound; until the radio is attached to this machine, the synthetic source
// below stands in.
class AudioSource {
 public:
  virtual ~AudioSource() = default;
  // Fills `frames` mono int16 samples. Returns false when the source has ended.
  virtual bool Read(int16_t* out, size_t frames) = 0;
  virtual int SampleRate() const = 0;
  virtual std::string Describe() const = 0;
};

// A steady tone. Deliberately NOT silence and NOT noise: silence hides a stream
// that has stopped, and noise is indistinguishable from real band audio in a
// recording. A pure tone is unmistakably synthetic to anyone who listens.
class ToneSource : public AudioSource {
 public:
  explicit ToneSource(int sample_rate = 22050, double hz = 700.0)
      : sample_rate_(sample_rate), hz_(hz) {}
  bool Read(int16_t* out, size_t frames) override;
  int SampleRate() const override { return sample_rate_; }
  std::string Describe() const override;

 private:
  int sample_rate_;
  double hz_;
  double phase_ = 0.0;
};

// The drop policy, pulled out so it can be tested directly. Which end gets
// dropped is the whole point, and a policy buried in a producer loop can only be
// tested through timing, which is how you end up with a test that cannot fail.
class BoundedChunkQueue {
 public:
  explicit BoundedChunkQueue(size_t max_chunks) : max_(max_chunks) {}

  // Drops the OLDEST when full. Keeping the newest is what keeps a listener on
  // live audio instead of playing out an ever-later recording of the band.
  void Push(std::vector<int16_t> chunk);
  bool Pop(std::vector<int16_t>& out);

  size_t size() const;
  size_t dropped() const { return dropped_; }

 private:
  mutable std::mutex mu_;
  std::deque<std::vector<int16_t>> q_;
  size_t max_;
  size_t dropped_ = 0;
};

class RxAudioStream {
 public:
  explicit RxAudioStream(std::unique_ptr<AudioSource> source);
  ~RxAudioStream();

  void Start();
  void Stop();

  void AddClient(std::shared_ptr<WsConnection> c);
  void RemoveClient(const std::shared_ptr<WsConnection>& c);
  size_t ClientCount() const;

  // The config frame every client gets first, before any binary frame. Field
  // names match the C# host exactly.
  std::string ConfigJson() const;

  // Matches the C# AudioStreamer, which trims to 10 before each push.
  static constexpr size_t kMaxQueuedChunks = 10;
  // 20 ms of audio per frame at 22050 Hz.
  static constexpr size_t kFramesPerChunk = 441;

  size_t DroppedChunks() const { return queue_.dropped(); }
  size_t QueueDepth() const { return queue_.size(); }
  size_t SentChunks() const { return sent_.load(); }
  std::string Backend() const { return source_->Describe(); }

  // ⚠️ Fed from the SAME audio the operator hears, not a second capture. A
  // recording taken from a separate stream would drift from what was heard and
  // would not include whatever the fan-out dropped.
  void SetRecorder(class Recorder* r) { recorder_ = r; }

 private:
  void ProduceLoop();
  void SendLoop();

  std::unique_ptr<AudioSource> source_;
  std::atomic<bool> running_{false};
  std::thread producer_, sender_;

  BoundedChunkQueue queue_{kMaxQueuedChunks};
  mutable std::mutex mu_;
  std::vector<std::shared_ptr<WsConnection>> clients_;

  std::atomic<size_t> sent_{0};
  class Recorder* recorder_ = nullptr;
};
