#include "audio.h"

#include "recorder.h"

#include <chrono>
#include <cmath>
#include <format>

bool ToneSource::Read(int16_t* out, size_t frames) {
  constexpr double kTwoPi = 6.283185307179586;
  const double step = kTwoPi * hz_ / sample_rate_;
  for (size_t i = 0; i < frames; ++i) {
    out[i] = static_cast<int16_t>(8000.0 * std::sin(phase_));
    phase_ += step;
    if (phase_ > kTwoPi) phase_ -= kTwoPi;
  }
  return true;
}

std::string ToneSource::Describe() const {
  return std::format("synthetic {}Hz tone @ {}Hz mono", static_cast<int>(hz_), sample_rate_);
}

void BoundedChunkQueue::Push(std::vector<int16_t> chunk) {
  std::lock_guard<std::mutex> lock(mu_);
  while (q_.size() >= max_) {
    q_.pop_front();
    ++dropped_;
  }
  q_.push_back(std::move(chunk));
}

bool BoundedChunkQueue::Pop(std::vector<int16_t>& out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (q_.empty()) return false;
  out = std::move(q_.front());
  q_.pop_front();
  return true;
}

size_t BoundedChunkQueue::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return q_.size();
}

RxAudioStream::RxAudioStream(std::unique_ptr<AudioSource> source)
    : source_(std::move(source)) {}

RxAudioStream::~RxAudioStream() { Stop(); }

std::string RxAudioStream::ConfigJson() const {
  return std::format(R"({{"type":"config","sample_rate":{},"channels":1,"bits_per_sample":16}})",
                     source_->SampleRate());
}

void RxAudioStream::Start() {
  if (running_.exchange(true)) return;
  producer_ = std::thread([this] { ProduceLoop(); });
  sender_ = std::thread([this] { SendLoop(); });
}

void RxAudioStream::Stop() {
  if (!running_.exchange(false)) return;
  if (producer_.joinable()) producer_.join();
  if (sender_.joinable()) sender_.join();
}

void RxAudioStream::AddClient(std::shared_ptr<WsConnection> c) {
  std::lock_guard<std::mutex> lock(mu_);
  clients_.push_back(std::move(c));
}

void RxAudioStream::RemoveClient(const std::shared_ptr<WsConnection>& c) {
  std::lock_guard<std::mutex> lock(mu_);
  std::erase(clients_, c);
}

size_t RxAudioStream::ClientCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return clients_.size();
}

// Produces at real time. That is not an optimisation, it is the contract: the
// device delivers audio at the sample rate and nothing downstream may run faster,
// or the receiver's buffer fills and every later frame arrives late.
void RxAudioStream::ProduceLoop() {
  const int rate = source_->SampleRate();
  const auto chunk_duration =
      std::chrono::microseconds(kFramesPerChunk * 1000000 / rate);
  auto next = std::chrono::steady_clock::now();

  while (running_.load()) {
    std::vector<int16_t> chunk(kFramesPerChunk);
    if (!source_->Read(chunk.data(), kFramesPerChunk)) break;

    // The peak of what was actually captured. One pass over 20 ms, and it is
    // the only reading on this path that can tell a live band from silence.
    {
      int peak = 0;
      for (const int16_t v : chunk) {
        const int a = v < 0 ? -static_cast<int>(v) : static_cast<int>(v);
        if (a > peak) peak = a;
      }
      const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      if (now_ms - peak_ms_.load() > kPeakWindowMs) {
        // Window expired: start a new one at this chunk rather than carrying the
        // old maximum forward, so the reading FALLS when the audio does.
        peak_ms_.store(now_ms);
        peak_.store(peak);
      } else {
        int prev = peak_.load();
        while (peak > prev && !peak_.compare_exchange_weak(prev, peak)) {}
      }
    }

    if (recorder_) recorder_->Feed(chunk.data(), chunk.size());
    // Trims before pushing, dropping the oldest, exactly as the C# streamer does.
    queue_.Push(std::move(chunk));
    next += chunk_duration;
    std::this_thread::sleep_until(next);
  }
}

// ONE writer. Every frame for every client goes out from this thread, so frames
// can never interleave on a socket.
void RxAudioStream::SendLoop() {
  while (running_.load()) {
    std::vector<int16_t> chunk;
    const bool got = queue_.Pop(chunk);
    std::vector<std::shared_ptr<WsConnection>> targets;
    {
      std::lock_guard<std::mutex> lock(mu_);
      targets = clients_;
    }
    if (!got || chunk.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    if (targets.empty()) continue;   // nobody listening; the frame is discarded

    for (auto& c : targets) {
      if (c->open()) c->SendBinary(chunk.data(), chunk.size() * sizeof(int16_t));
    }
    sent_.fetch_add(1);
  }
}

// ⚠️ STALE READINGS REPORT ZERO, they do not linger. If the producer thread has
// stopped - the capture died, the device went away - the last peak it managed is
// not a description of now, and showing it would say "audio is flowing" about a
// stream that ended. Two windows of nothing is not a live band.
int RxAudioStream::RxPeak() const {
  const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (peak_ms_.load() == 0) return 0;
  if (now_ms - peak_ms_.load() > kPeakWindowMs * 2) return 0;
  return peak_.load();
}
