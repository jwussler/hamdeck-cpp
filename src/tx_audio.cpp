#include "tx_audio.h"

#include <chrono>
#include <algorithm>
#include <cstring>

TxAudioReceiver::TxAudioReceiver(std::unique_ptr<TxAudioSink> sink)
    : sink_(std::move(sink)) {}

TxAudioReceiver::~TxAudioReceiver() { Stop(); }

bool TxAudioReceiver::Claim(const std::string& who) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!holder_.empty() && holder_ != who) return false;
  holder_ = who;
  return true;
}

void TxAudioReceiver::Release(const std::string& who) {
  std::lock_guard<std::mutex> lock(mu_);
  if (holder_ == who) {
    holder_.clear();
    queue_.clear();   // never carry one operator's audio into another's over
  }
}

bool TxAudioReceiver::HeldBy(const std::string& who) const {
  std::lock_guard<std::mutex> lock(mu_);
  return holder_ == who;
}

std::string TxAudioReceiver::Holder() const {
  std::lock_guard<std::mutex> lock(mu_);
  return holder_;
}

bool TxAudioReceiver::Accept(const char* data, size_t bytes, bool keyed) {
  // A 16-bit stream must arrive in whole samples. An odd byte count means the
  // framing is wrong, and interpreting it anyway shifts every following sample
  // by one byte - which is loud noise on the air, not a subtle glitch.
  if (bytes == 0 || (bytes % sizeof(int16_t)) != 0) return false;

  std::vector<int16_t> chunk(bytes / sizeof(int16_t));
  std::memcpy(chunk.data(), data, bytes);

  // Peak of what actually arrived, so silence can be told from audio. Cheap: one
  // pass over a 20 ms chunk, and it is the only reading here that distinguishes
  // a working microphone from a perfectly healthy pipe full of nothing.
  int peak = 0;
  for (const int16_t v : chunk) {
    const int a = v < 0 ? -static_cast<int>(v) : static_cast<int>(v);
    if (a > peak) peak = a;
  }
  int prev = peak_.load();
  while (peak > prev && !peak_.compare_exchange_weak(prev, peak)) {}

  std::lock_guard<std::mutex> lock(mu_);
  if (queue_.size() >= kMaxQueuedChunks) {
    // ⚠️ TRIM ONLY BETWEEN OVERS (CARRYOVER.md section 3). Dropping audio while
    // the rig is KEYED is audible - a syllable vanishes mid-sentence. Between
    // overs it costs nothing, and with the mic open there is always idle time.
    // So while keyed we accept the latency and let the queue run long; the
    // moment the operator unkeys, the backlog is trimmed and the next over
    // starts at the target depth however far the link drifted.
    if (!keyed) {
      queue_.pop_front();
      dropped_.fetch_add(1);
    }
  }
  queue_.push_back(std::move(chunk));
  accepted_.fetch_add(1);
  return true;
}

size_t TxAudioReceiver::Pump(size_t max_chunks) {
  const int rate = sink_->SampleRate();
  const int device_ms = sink_->QueuedMs();

  // ── React to underruns ────────────────────────────────────────────────────
  const long xruns = sink_->Xruns();
  const auto now = std::chrono::steady_clock::now();
  if (xruns > last_xruns_) {
    last_xruns_ = xruns;
    last_xrun_at_ = now;
    // The cushion was too thin. Grow it, and start filling again before feeding.
    target_ms_ = std::min(target_ms_ + kXrunGrowMs, kTargetCeilMs);
    prerolled_ = false;
  } else if (last_xrun_at_.time_since_epoch().count() != 0 &&
             now - last_xrun_at_ > std::chrono::seconds(kDecayAfterSec)) {
    // Quiet for a while: trim the latency back down. Every over then STARTS at
    // the lower target, however far the link drifted earlier.
    target_ms_ = std::max(target_ms_ - kDecayStepMs, kTargetFloorMs);
    last_xrun_at_ = now;
  }

  // ── Pre-roll is the DEVICE's job now ─────────────────────────────────────
  // start_threshold (see alsa_audio.cpp) holds playback until the cushion
  // exists. Holding audio back here as well starved the device instead of
  // filling it. Kept only for sinks that cannot report a queue depth at all.
  if (!prerolled_ && device_ms < 0) {
    size_t queued_frames = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto& c : queue_) queued_frames += c.size();
    }
    const int held_ms = rate > 0 ? static_cast<int>(queued_frames * 1000 / rate) : 0;

    // ⚠️ Never wait for more than the queue can physically hold, or the pre-roll
    // can never be satisfied and audio stops for good. Belt and braces alongside
    // the queue being sized above the ceiling.
    const int capacity_ms =
        rate > 0 ? static_cast<int>(kMaxQueuedChunks * kFramesPerChunkHint * 1000 / rate)
                 : target_ms_;
    const int need_ms = std::min(target_ms_, capacity_ms * 3 / 4);

    if (held_ms + std::max(device_ms, 0) < need_ms) return 0;   // keep filling
    prerolled_ = true;
  }

  // ── Hold the fill level, do not dump the queue ────────────────────────────
  // ⚠️ Draining everything available each cycle produces a SAWTOOTH: the device
  // is stuffed far past the target, drains to empty, underruns, and the cycle
  // repeats. Measured before this: the queue swinging 58 ms to 514 ms with an
  // underrun most seconds.
  //
  // Feed only enough to reach the target, then stop. "Adaptive buffering" is
  // about controlling the FILL LEVEL, not about having a big buffer.
  const int chunk_ms =
      rate > 0 ? static_cast<int>(kFramesPerChunkHint * 1000 / rate) : 20;
  size_t written = 0;
  for (size_t i = 0; i < max_chunks; ++i) {
    const int on_device = sink_->QueuedMs();
    // Stop once the target is REACHED, not once the next chunk would exceed it.
    // The lookahead form stalls one chunk short for ever when the target and the
    // device's start threshold are close together.
    if (on_device >= 0 && on_device >= target_ms_) break;
    (void)chunk_ms;

    std::vector<int16_t> chunk;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (queue_.empty()) break;
      chunk = std::move(queue_.front());
      queue_.pop_front();
    }
    if (!sink_->Write(chunk.data(), chunk.size())) break;
    ++written;
  }
  return written;
}

size_t TxAudioReceiver::QueueDepth() const {
  std::lock_guard<std::mutex> lock(mu_);
  return queue_.size();
}

void TxAudioReceiver::Start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { PumpLoop(); });
}

void TxAudioReceiver::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void TxAudioReceiver::PumpLoop() {
  // 20ms cadence, matching the chunk size the client sends. A real ALSA sink
  // paces itself by blocking; the null sink does not, so the loop sets the pace.
  while (running_.load()) {
    Pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}
