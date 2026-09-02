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

// ⚠️ UTC, ISO 8601, ALWAYS - and deliberately not the same clock the FILENAME
// uses. The name is local time because that is how the operator reads a
// directory (house style, MM-DD-YYYY); the sidecar is UTC because that is what
// a log is in. Deriving one from the other is the whole class of timezone bug
// that has bitten this operator's other tooling.
std::string Utc(std::chrono::system_clock::time_point tp) {
  const std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string JsonEscape(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (static_cast<unsigned char>(c) >= 0x20) out += c;
  }
  return out;
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

Recorder::Result Recorder::Start(const std::string& tag) {
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
  r = OpenFile(tag);
  // ⚠️ recording_ is set from whether the file actually opened, never from
  // having been asked. That is the whole lesson of the route this replaces.
  recording_.store(r.ok);
  if (r.ok) {
    file_started_ = std::chrono::system_clock::now();
    file_start_prov_ = Ask();
    overs_.clear();
    r.message = "recording";
  }
  return r;
}

Recorder::Result Recorder::Stop(const std::string& trigger) {
  Result r;
  std::lock_guard<std::mutex> lock(mu_);
  if (!file_) {
    r.message = "not recording";
    return r;
  }
  r.filename = file_path_;
  const int secs = static_cast<int>(file_frames_ / rate_);
  // ⚠️ Before CloseFile(), which clears file_path_. The sidecar is written for
  // the file that just closed, from the state that produced it.
  WriteSidecar(file_path_, trigger, file_started_, file_start_prov_, &overs_);
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
  // ⚠️ THE REPLAY'S START TIME IS IN THE PAST. The whole point of the ring is
  // that it holds what happened BEFORE the operator pressed anything, so
  // stamping the sidecar with "now" would file the audio minutes after the
  // exchange it contains and match it to the wrong QSO - or to none. Derived
  // from the sample count, which is what the audio actually is.
  const auto secs = static_cast<int>(snap.size() / rate_);
  const auto began = std::chrono::system_clock::now() - std::chrono::seconds(secs);
  // ⚠️ Provenance is read at save time, not capture time: the frequency is
  // where the rig is NOW, which is where it was during the buffer only if it
  // has not moved. Good enough to match on, and the sidecar says start and end
  // separately so a QSY between them is visible rather than hidden.
  WriteSidecar(path, "replay", began, Ask(), nullptr);

  r.ok = true;
  r.filename = path;
  r.message = std::format("saved {} s from the buffer", secs);
  return r;
}

void Recorder::UpdateProvenance(bool connected, long long freq_hz,
                                const std::string& mode) {
  std::lock_guard<std::mutex> lock(prov_mu_);
  prov_.connected = connected;
  prov_.freq_hz = freq_hz;
  prov_.mode = mode;
}

Recorder::Provenance Recorder::Ask() const {
  // ⚠️ NO GUESSING. Until the poller has handed over a connected reading, the
  // sidecar says connected:false and carries no frequency - it does not carry a
  // stale one. A recording filed under the wrong band is worse than one filed
  // under none, because the wrong one gets matched to a QSO.
  std::lock_guard<std::mutex> lock(prov_mu_);
  return prov_;
}

void Recorder::NoteOver(bool keyed) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!file_) return;  // overs only mean something inside a recording
  const auto now = std::chrono::system_clock::now();
  if (keyed) {
    if (!overs_.empty() && overs_.back().open) return;  // already keyed
    overs_.push_back({now, now, true});
  } else if (!overs_.empty() && overs_.back().open) {
    overs_.back().end = now;
    overs_.back().open = false;
  }
}

void Recorder::WriteSidecar(const std::string& wav_path, const std::string& trigger,
                            std::chrono::system_clock::time_point started,
                            const Provenance& at_start,
                            const std::vector<Over>* overs_in) const {
  const Provenance at_end = Ask();
  const auto ended = std::chrono::system_clock::now();

  std::string overs = "null";
  if (overs_in) {
    overs = "[";
    for (size_t i = 0; i < overs_in->size(); ++i) {
      const auto& o = (*overs_in)[i];
    // An over still open at close is a recording that ended mid-transmission -
    // reported as such rather than given an invented end.
      overs += std::format(R"({}{{"start_utc":"{}","end_utc":{}}})",
                           i ? "," : "", Utc(o.start),
                           o.open ? "null" : ("\"" + Utc(o.end) + "\""));
    }
    overs += "]";
  }

  const std::string path = wav_path + ".json";
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;  // the audio is saved; a missing sidecar must not lose it
  const std::string json = std::format(
      "{{\n"
      R"(  "file": "{}",)" "\n"
      R"(  "trigger": "{}",)" "\n"
      R"(  "started_utc": "{}",)" "\n"
      R"(  "ended_utc": "{}",)" "\n"
      R"(  "rig_connected": {},)" "\n"
      R"(  "freq_hz_start": {},)" "\n"
      R"(  "freq_hz_end": {},)" "\n"
      R"(  "mode": "{}",)" "\n"
      R"(  "sample_rate": {},)" "\n"
      R"(  "channels": 1,)" "\n"
      R"(  "overs": {})" "\n"
      "}}\n",
      JsonEscape(std::filesystem::path(wav_path).filename().string()),
      JsonEscape(trigger), Utc(started), Utc(ended),
      at_start.connected ? "true" : "false",
      at_start.connected ? at_start.freq_hz : 0,
      at_end.connected ? at_end.freq_hz : 0,
      JsonEscape(at_start.mode), rate_, overs);
  std::fwrite(json.data(), 1, json.size(), f);
  std::fclose(f);
}

int Recorder::recorded_seconds() const {
  std::lock_guard<std::mutex> lock(mu_);
  return file_ ? static_cast<int>(file_frames_ / rate_) : 0;
}
