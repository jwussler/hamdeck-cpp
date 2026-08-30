#include "alsa_audio.h"

#include <fstream>
#include <format>
#include <sstream>

namespace {

// Configure a PCM for mono S16 at `rate`. Returns an empty string on success.
std::string Configure(snd_pcm_t* pcm, int rate, snd_pcm_uframes_t buffer_frames,
                      snd_pcm_uframes_t period_frames) {
  snd_pcm_hw_params_t* hw = nullptr;
  snd_pcm_hw_params_alloca(&hw);
  if (snd_pcm_hw_params_any(pcm, hw) < 0) return "no configurations available";
  if (snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    return "interleaved access unsupported";
  if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE) < 0)
    return "S16_LE unsupported";
  if (snd_pcm_hw_params_set_channels(pcm, hw, 1) < 0) return "mono unsupported";

  // ⚠️ EXACT RATE, NOT "near". snd_pcm_hw_params_set_rate_near would silently
  // give a different rate than asked for - and the device does exactly that at
  // 8000, handing back 11025 worth of data. Audio at the wrong rate is a
  // chipmunk on the air, so a rate this device cannot do must FAIL here rather
  // than succeed quietly.
  if (snd_pcm_hw_params_set_rate(pcm, hw, rate, 0) < 0)
    return std::format("device cannot do exactly {} Hz", rate);

  if (snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer_frames) < 0)
    return "could not set buffer size";
  if (snd_pcm_hw_params_set_period_size_near(pcm, hw, &period_frames, nullptr) < 0)
    return "could not set period size";
  if (snd_pcm_hw_params(pcm, hw) < 0) return "could not apply parameters";
  return {};
}

// ⚠️ PRE-ROLL BELONGS TO ALSA, NOT TO US.
//
// start_threshold tells the device: do not begin playing until this many frames
// are queued. That is exactly the cushion the adaptive buffering wants, and
// letting ALSA enforce it is far more reliable than holding audio back in user
// space - a hand-rolled version fed the device one chunk per cycle, which is
// precisely real time and never accumulated any cushion at all. Measured: 1512
// underruns in 18 seconds doing it by hand, against 290 doing nothing.
std::string ConfigureStart(snd_pcm_t* pcm, snd_pcm_uframes_t start_frames,
                           snd_pcm_uframes_t period_frames) {
  snd_pcm_sw_params_t* sw = nullptr;
  snd_pcm_sw_params_alloca(&sw);
  if (snd_pcm_sw_params_current(pcm, sw) < 0) return "no current sw params";
  if (snd_pcm_sw_params_set_start_threshold(pcm, sw, start_frames) < 0)
    return "could not set start threshold";
  if (snd_pcm_sw_params_set_avail_min(pcm, sw, period_frames) < 0)
    return "could not set avail_min";
  if (snd_pcm_sw_params(pcm, sw) < 0) return "could not apply sw params";
  return {};
}

}  // namespace

// ── Capture ────────────────────────────────────────────────────────────────
AlsaCapture::~AlsaCapture() {
  if (pcm_) snd_pcm_close(pcm_);
}

bool AlsaCapture::Open(const std::string& device, int sample_rate) {
  if (snd_pcm_open(&pcm_, device.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0) {
    error_ = "could not open " + device;
    pcm_ = nullptr;
    return false;
  }
  // ~0.5 s of buffer, 20 ms periods. Capture wants slack: a late reader loses
  // audio outright, and there is no way to get it back.
  error_ = Configure(pcm_, sample_rate, sample_rate / 2, sample_rate / 50);
  if (!error_.empty()) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    return false;
  }
  device_ = device;
  sample_rate_ = sample_rate;
  return snd_pcm_prepare(pcm_) >= 0;
}

bool AlsaCapture::Read(int16_t* out, size_t frames) {
  if (!pcm_) return false;
  size_t got = 0;
  while (got < frames) {
    const snd_pcm_sframes_t n = snd_pcm_readi(pcm_, out + got, frames - got);
    if (n == -EPIPE) {
      // Overrun: the reader fell behind. Recover and carry on - reporting a
      // failure here would tear down a stream that is merely late.
      ++xruns_;
      snd_pcm_prepare(pcm_);
      continue;
    }
    if (n == -EAGAIN) continue;
    if (n < 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

std::string AlsaCapture::Describe() const {
  if (!pcm_) return "alsa capture (closed): " + error_;
  return std::format("alsa {} @ {}Hz mono/16 (xruns {})", device_, sample_rate_, xruns_);
}

// ── Playback ───────────────────────────────────────────────────────────────
AlsaPlayback::~AlsaPlayback() {
  if (pcm_) snd_pcm_close(pcm_);
}

bool AlsaPlayback::Open(const std::string& device, int sample_rate) {
  if (snd_pcm_open(&pcm_, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
    error_ = "could not open " + device;
    pcm_ = nullptr;
    return false;
  }
  // ⚠️ A GENEROUS BUFFER, WITH THE FILL LEVEL CONTROLLED ELSEWHERE.
  // CARRYOVER.md section 3: give the device room, then manage how full it is.
  // A small buffer just trades latency for underruns, and an underrun mid-word
  // is worse than 100 ms of delay.
  error_ = Configure(pcm_, sample_rate, sample_rate / 2, sample_rate / 50);
  if (error_.empty()) {
    // ⚠️ START THRESHOLD MUST SIT BELOW THE FEED TARGET, NOT ON IT.
    //
    // Set to the same 150 ms the adaptive loop aims for, the two thresholds
    // deadlocked: the feeder stopped at 140 ms because one more 20 ms chunk
    // would overshoot the target, so the queue never reached the 150 ms the
    // device needed to start. Result: a perfectly steady 140 ms of audio, zero
    // underruns, and total silence - the stream sat in PREPARED for ever while
    // 923 chunks were dropped behind it.
    //
    // 80 ms is the adaptive floor, so any target the loop picks clears it.
    error_ = ConfigureStart(pcm_, sample_rate * 80 / 1000, sample_rate / 50);
  }
  if (!error_.empty()) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    return false;
  }
  device_ = device;
  sample_rate_ = sample_rate;
  return snd_pcm_prepare(pcm_) >= 0;
}

bool AlsaPlayback::Write(const int16_t* samples, size_t frames) {
  if (!pcm_) return false;
  size_t sent = 0;
  while (sent < frames) {
    const snd_pcm_sframes_t n = snd_pcm_writei(pcm_, samples + sent, frames - sent);
    if (n == -EPIPE) {
      // Underrun: the device ran dry. It is recoverable, and it is also the
      // signal the adaptive buffering uses to grow its target.
      ++xruns_;
      snd_pcm_prepare(pcm_);
      continue;
    }
    if (n == -EAGAIN) continue;
    if (n < 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

long AlsaPlayback::QueuedFrames() const {
  if (!pcm_) return -1;
  snd_pcm_sframes_t delay = 0;
  if (snd_pcm_delay(pcm_, &delay) < 0) return -1;
  return delay < 0 ? 0 : static_cast<long>(delay);
}

int AlsaPlayback::QueuedMs() const {
  const long frames = QueuedFrames();
  if (frames < 0 || sample_rate_ <= 0) return -1;
  return static_cast<int>(frames * 1000 / sample_rate_);
}

std::string AlsaPlayback::Describe() const {
  if (!pcm_) return "alsa playback (closed): " + error_;
  return std::format("alsa {} @ {}Hz mono/16 (xruns {})", device_, sample_rate_, xruns_);
}

// ── /proc cross-check ──────────────────────────────────────────────────────
long ProcAsoundDelayFrames(const std::string& card_hint) {
  // /proc/asound/<card> is a symlink to the real card directory, so the path is
  // derivable from an ALSA device name like "hw:CODEC,0".
  std::string card = card_hint;
  if (const auto colon = card.find(':'); colon != std::string::npos) {
    card = card.substr(colon + 1);
  }
  if (const auto comma = card.find(','); comma != std::string::npos) {
    card = card.substr(0, comma);
  }
  const std::string path = "/proc/asound/" + card + "/pcm0p/sub0/status";
  std::ifstream f(path);
  if (!f) return -1;
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("delay", 0) != 0) continue;
    const auto colon = line.find(':');
    if (colon == std::string::npos) return -1;
    try {
      return std::stol(line.substr(colon + 1));
    } catch (const std::exception&) {
      return -1;
    }
  }
  return -1;   // "delay" only appears while the stream is running
}
