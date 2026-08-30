#include "tx_audio.h"

#include <chrono>
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
  size_t written = 0;
  for (size_t i = 0; i < max_chunks; ++i) {
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
