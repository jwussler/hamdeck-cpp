#include "recorder.h"

#include <algorithm>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>

namespace {

void PutLE32(unsigned char* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}
void PutLE16(unsigned char* p, uint16_t v) {
  p[0] = v & 0xFF; p[1] = v >> 8;
}

// MM-DD-YYYY-HHMMSS, so a directory of recordings reads the way the operator
// writes dates rather than the way a machine sorts them.
std::string Stamp() {
  const auto now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%m-%d-%Y-%H%M%S", &tm);
  return buf;
}

}  // namespace

Recorder::Recorder(std::string dir, int sample_rate, int buffer_seconds,
                   int max_seconds, int warn_seconds)
    : dir_(std::move(dir)),
      rate_(sample_rate),
      ring_max_frames_(static_cast<size_t>(sample_rate) * buffer_seconds),
      max_seconds_(max_seconds),
      warn_seconds_(warn_seconds) {
  if (dir_.empty()) {
    reason_ = "no record_path configured";
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) {
    reason_ = "cannot create " + dir_;
    return;
  }
  // ⚠️ Prove it is WRITABLE now, not at the moment the operator presses record.
  // Discovering the directory is read-only halfway through something worth
  // keeping is the worst possible time.
  const std::string probe = dir_ + "/.hamdeck-write-test";
  if (std::FILE* f = std::fopen(probe.c_str(), "wb")) {
    std::fclose(f);
    std::filesystem::remove(probe, ec);
    available_ = true;
    buffering_.store(true);
  } else {
    reason_ = dir_ + " is not writable";
  }
}

Recorder::~Recorder() { CloseFile(); }

void Recorder::Feed(const int16_t* samples, size_t frames) {
  if (!available_) return;
  frames_fed_.fetch_add(frames);

  std::lock_guard<std::mutex> lock(mu_);
  ring_.insert(ring_.end(), samples, samples + frames);
  while (ring_.size() > ring_max_frames_) ring_.pop_front();

  if (file_) {
    // ⚠️ Trim the write to the ceiling BEFORE writing it. Writing the whole
    // chunk and then noticing overruns the cap by up to one chunk, which makes
    // record_max_seconds a suggestion rather than a limit.
    size_t writable = frames;
    if (max_seconds_ > 0) {
      const size_t cap = static_cast<size_t>(max_seconds_) * rate_;
      writable = file_frames_ >= cap ? 0 : std::min(frames, cap - file_frames_);
    }
    if (writable) std::fwrite(samples, sizeof(int16_t), writable, file_);
    file_frames_ += static_cast<uint32_t>(writable);
    // ⚠️ A hard ceiling. An unattended recording that nobody stops fills the
    // disk, and a full disk on the station host is a much bigger problem than a
    // truncated recording.
    if (max_seconds_ > 0 &&
        file_frames_ >= static_cast<uint32_t>(max_seconds_) * rate_) {
      const auto path = file_path_;
      // Close inline; the caller is the audio thread, so keep it short.
      const uint32_t bytes = file_frames_ * 2;
      std::fseek(file_, 0, SEEK_SET);
      WriteWavHeader(file_, bytes);
      std::fclose(file_);
      file_ = nullptr;
      recording_.store(false);
    }
  }
}

bool Recorder::WriteWavHeader(std::FILE* f, uint32_t data_bytes) const {
  unsigned char h[44];
  std::memcpy(h, "RIFF", 4);
  PutLE32(h + 4, 36 + data_bytes);
  std::memcpy(h + 8, "WAVEfmt ", 8);
  PutLE32(h + 16, 16);            // fmt chunk size
  PutLE16(h + 20, 1);             // PCM
  PutLE16(h + 22, 1);             // mono
  PutLE32(h + 24, rate_);
  PutLE32(h + 28, rate_ * 2);     // byte rate
  PutLE16(h + 32, 2);             // block align
  PutLE16(h + 34, 16);            // bits
  std::memcpy(h + 36, "data", 4);
  PutLE32(h + 40, data_bytes);
  return std::fwrite(h, 1, sizeof(h), f) == sizeof(h);
}

Recorder::Result Recorder::OpenFile(const std::string& tag) {
  Result r;
  const std::string path = std::format("{}/hamdeck-{}-{}.wav", dir_, tag, Stamp());
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    r.message = "could not open " + path;
    return r;
  }
  // Placeholder header; the real sizes are written on close.
  WriteWavHeader(f, 0);
  file_ = f;
  file_path_ = path;
  file_frames_ = 0;
  r.ok = true;
  r.filename = path;
  return r;
}

void Recorder::CloseFile() {
  if (!file_) return;
  // ⚠️ Rewrite the header with the real length. A WAV whose header still says
  // zero plays as an empty file in most players - the audio is all there and
  // nothing will play it.
  std::fseek(file_, 0, SEEK_SET);
  WriteWavHeader(file_, file_frames_ * 2);
  std::fclose(file_);
  file_ = nullptr;
}

Recorder::Result Recorder::Start() {
  Result r;
  if (!available_) {
    r.message = reason_;
    return r;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (file_) {
    // ⚠️ NOT ok. Answering "started" to a start that started nothing is exactly
    // the reference bug this class exists to avoid. The operator pressed record
    // twice; tell them the first one is still running and which file it is.
    r.ok = false;
    r.filename = file_path_;
    r.message = "already recording since " + std::to_string(file_frames_ / rate_) +
                " s ago; stop it first";
    return r;
  }
  r = OpenFile("rec");
  // ⚠️ recording_ is set from whether the file actually opened, never from
  // having been asked. That is the whole lesson of the route this replaces.
  recording_.store(r.ok);
  if (r.ok) r.message = "recording";
  return r;
}

Recorder::Result Recorder::Stop() {
  Result r;
  std::lock_guard<std::mutex> lock(mu_);
  if (!file_) {
    r.message = "not recording";
    return r;
  }
  r.filename = file_path_;
  const int secs = static_cast<int>(file_frames_ / rate_);
  CloseFile();
  recording_.store(false);
  r.ok = true;
  r.message = std::format("stopped after {} s", secs);
  return r;
}

Recorder::Result Recorder::SaveReplay() {
  Result r;
  if (!available_) {
    r.message = reason_;
    return r;
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (ring_.empty()) {
    r.message = "replay buffer is empty";
    return r;
  }
  const std::string path = std::format("{}/hamdeck-replay-{}.wav", dir_, Stamp());
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    r.message = "could not open " + path;
    return r;
  }
  const std::vector<int16_t> snap(ring_.begin(), ring_.end());
  WriteWavHeader(f, static_cast<uint32_t>(snap.size() * 2));
  std::fwrite(snap.data(), sizeof(int16_t), snap.size(), f);
  std::fclose(f);
  r.ok = true;
  r.filename = path;
  r.message = std::format("saved {} s from the buffer",
                          static_cast<int>(snap.size() / rate_));
  return r;
}

int Recorder::recorded_seconds() const {
  std::lock_guard<std::mutex> lock(mu_);
  return file_ ? static_cast<int>(file_frames_ / rate_) : 0;
}
