// The receive level.
//
// ⚠️ WRITTEN BECAUSE THE OPERATOR SAID "NO AUDIO IS FLOWING" AND NOTHING HERE
// COULD ANSWER. The transmit side has had a peak since the night a perfectly
// healthy audio chain carried digital silence to the radio; the receive side had
// no measurement at all, so a live band and a dead one produced identical
// readings - capture RUNNING, zero xruns, frames moving.
//
// The three things this has to do, and each one is a way it could be useless:
//   1. read ZERO on silence - or it cannot report the failure it exists for;
//   2. read the actual level on audio - not merely non-zero;
//   3. FALL when the audio stops. A high-water mark that only rises would pass
//      the first two and still be unable to answer "is it flowing NOW", which is
//      the question. tx_peak decays for exactly this reason.

#include "check.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "../src/audio.h"

namespace {

// A source under the test's control: it hands out whatever level is asked for.
class LevelSource : public AudioSource {
 public:
  void SetLevel(int16_t v) { level_ = v; }
  bool Read(int16_t* out, size_t frames) override {
    for (size_t i = 0; i < frames; ++i) out[i] = level_;
    // Real capture blocks for the duration of a chunk; without that this loop
    // spins and the test measures a CPU, not a stream.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return true;
  }
  int SampleRate() const override { return 22050; }
  std::string Describe() const override { return "test level source"; }

 private:
  int16_t level_ = 0;
};

}  // namespace

int main() {
  auto src = std::make_unique<LevelSource>();
  LevelSource* raw = src.get();
  RxAudioStream rx(std::move(src));

  // Before anything has been captured there is no reading, and inventing one
  // would be the confident-wrong-answer failure this repo keeps hitting.
  CHECK(rx.RxPeak() == 0);

  rx.Start();

  // 1. Silence must read zero, not "small".
  raw->SetLevel(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(rx.RxPeak() == 0);

  // 2. Audio must read its actual level.
  raw->SetLevel(8000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const int loud = rx.RxPeak();
  CHECK(loud == 8000);

  // 3. ⚠️ AND IT MUST COME BACK DOWN. This is the assertion that separates a
  // level from a high-water mark: quieter audio has to read quieter WHILE it is
  // still flowing, or an operator turning something down sees no change.
  raw->SetLevel(1000);
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));   // past the window
  const int quiet = rx.RxPeak();
  CHECK(quiet == 1000);
  CHECK(quiet < loud);

  // 4. And silence after audio reads zero again, rather than holding the last
  // number and reporting a dead band as a live one.
  raw->SetLevel(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  CHECK(rx.RxPeak() == 0);

  rx.Stop();
  std::printf("rx peak: silence 0, audio 8000, quieter 1000, silence again 0\n");
  std::printf("PASS\n");
  return 0;
}
