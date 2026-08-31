#include "pcm_convert.h"

#include <cmath>

PcmConverter::PcmConverter(int src_rate, int src_channels, int dst_rate)
    : src_rate_(src_rate > 0 ? src_rate : dst_rate),
      src_channels_(src_channels > 0 ? src_channels : 1),
      dst_rate_(dst_rate > 0 ? dst_rate : 48000) {}

std::vector<int16_t> PcmConverter::Convert(const int16_t* in, std::size_t samples) {
  std::vector<int16_t> mono;
  if (in == nullptr || samples == 0) return mono;

  // ── Downmix ───────────────────────────────────────────────────────────────
  // ⚠️ AVERAGE the channels, never just take the left one. A USB microphone that
  // enumerates as stereo commonly puts the capsule on ONE channel and silence on
  // the other; picking a channel is a coin flip between full audio and none, and
  // the wrong side of it is a dead transmitter that looks like it is working.
  if (src_channels_ <= 1) {
    mono.assign(in, in + samples);
  } else {
    // A partial frame at the end of a chunk is dropped: carrying it would need
    // per-channel state for one sample of audio, and the next readyRead brings
    // the rest of it in under a millisecond.
    const std::size_t frames = samples / static_cast<std::size_t>(src_channels_);
    mono.reserve(frames);
    for (std::size_t f = 0; f < frames; ++f) {
      int32_t sum = 0;
      for (int c = 0; c < src_channels_; ++c) {
        sum += in[f * static_cast<std::size_t>(src_channels_) + static_cast<std::size_t>(c)];
      }
      mono.push_back(static_cast<int16_t>(sum / src_channels_));
    }
  }

  if (src_rate_ == dst_rate_ || mono.empty()) return mono;

  // ── Resample ──────────────────────────────────────────────────────────────
  // The previous chunk's last sample is prepended so interpolation can span the
  // boundary. Without it every chunk starts from silence and the seam is audible.
  // ⚠️ The FIRST chunk has no predecessor, and inventing one by repeating the
  // first sample adds a sample that was never captured - it made one second of
  // 44.1k resample to 48001 samples. Prepend only when there is a real previous
  // sample to prepend.
  std::vector<int16_t> buf;
  if (primed_) {
    buf.reserve(mono.size() + 1);
    buf.push_back(prev_);
    buf.insert(buf.end(), mono.begin(), mono.end());
  } else {
    buf = mono;
    primed_ = true;
  }

  const double ratio = static_cast<double>(src_rate_) / static_cast<double>(dst_rate_);
  std::vector<int16_t> out;
  out.reserve(static_cast<std::size_t>(static_cast<double>(mono.size()) / ratio) + 2);

  while (pos_ + 1.0 < static_cast<double>(buf.size())) {
    const std::size_t i = static_cast<std::size_t>(pos_);
    const double frac = pos_ - static_cast<double>(i);
    const double v =
        static_cast<double>(buf[i]) * (1.0 - frac) + static_cast<double>(buf[i + 1]) * frac;
    out.push_back(static_cast<int16_t>(std::lround(v)));
    pos_ += ratio;
  }

  prev_ = buf.back();
  pos_ -= static_cast<double>(buf.size() - 1);
  if (pos_ < 0.0) pos_ = 0.0;
  return out;
}
