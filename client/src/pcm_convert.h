#pragma once

// Turn a capture device's NATIVE pcm into the host's wire format: 48000 Hz,
// 16-bit, mono.
//
// ⚠️ THIS EXISTS BECAUSE THE CLIENT USED TO REFUSE THE OPERATOR'S MICROPHONE.
// OpenMic() asked for exactly 48000/16/mono and gave up when the device said no,
// which on a stereo-only USB microphone means the panel arms, reports "armed",
// and transmits silence - the rig keys into nothing and the operator has no
// meter to notice. A capture path must take what the device offers and convert;
// the wire format is the HOST's requirement, not the microphone's.
//
// Linear interpolation, deliberately. It is not the best resampler, but it is
// one that can be read and tested, and voice at 44.1k -> 48k is a 1.088 ratio -
// nowhere near the range where the filter quality is what limits intelligibility.

#include <cstddef>
#include <cstdint>
#include <vector>

class PcmConverter {
 public:
  PcmConverter() = default;
  PcmConverter(int src_rate, int src_channels, int dst_rate);

  // Interleaved int16 samples in; mono int16 at dst_rate out.
  //
  // ⚠️ The resampler carries state ACROSS calls - a fractional read position and
  // the last source sample. Resetting it per chunk would restart the phase every
  // 20 ms and put a discontinuity at every chunk boundary, which is a buzz on
  // the air at the chunk rate rather than an occasional glitch.
  std::vector<int16_t> Convert(const int16_t* in, std::size_t samples);

  // True when the device already speaks the wire format and Convert() copies.
  bool passthrough() const {
    return src_channels_ == 1 && src_rate_ == dst_rate_;
  }

  // Drop the carried phase. Call on unkey, never mid-over.
  void Reset() {
    pos_ = 0.0;
    prev_ = 0;
    primed_ = false;
  }

  int src_rate() const { return src_rate_; }
  int src_channels() const { return src_channels_; }

 private:
  int src_rate_ = 48000;
  int src_channels_ = 1;
  int dst_rate_ = 48000;

  double pos_ = 0.0;      // read position, in source samples, carried between calls
  int16_t prev_ = 0;      // last source sample of the previous chunk
  bool primed_ = false;
};
