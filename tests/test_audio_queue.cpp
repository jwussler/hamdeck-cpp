// The RX audio drop policy.
//
// "Bounded" is easy to get right by accident and easy to get backwards. The
// assertion that matters is not that the queue stopped growing - it is WHICH
// chunks survived. Dropping the newest also bounds the queue, and also passes a
// size check, while leaving the listener playing an ever-later recording of the
// band. So this test identifies every surviving chunk.

#include <cassert>
#include <cstdio>
#include <vector>

#include "../src/audio.h"

int main() {
  BoundedChunkQueue q(10);

  // Push 15 identifiable chunks: chunk n is a single sample of value n.
  for (int i = 0; i < 15; ++i) q.Push(std::vector<int16_t>{static_cast<int16_t>(i)});

  assert(q.size() == 10);
  assert(q.dropped() == 5);
  std::printf("bound:    15 pushed, size=%zu, dropped=%zu\n", q.size(), q.dropped());

  // The survivors must be 5..14 - the NEWEST ten. If this comes back 0..9 the
  // policy is drop-newest and every listener is permanently behind.
  std::vector<int16_t> out;
  for (int expect = 5; expect < 15; ++expect) {
    assert(q.Pop(out));
    assert(out.size() == 1);
    if (out[0] != expect) {
      std::printf("FAIL: expected chunk %d, got %d - policy is dropping the WRONG END\n",
                  expect, out[0]);
      return 1;
    }
  }
  assert(!q.Pop(out));
  std::printf("policy:   survivors are the newest 10 (chunks 5..14), oldest dropped\n");

  // The tone source must produce the wire format the client expects.
  ToneSource tone(22050, 700.0);
  assert(tone.SampleRate() == 22050);
  std::vector<int16_t> buf(441);
  assert(tone.Read(buf.data(), buf.size()));
  bool nonzero = false;
  for (int16_t v : buf) if (v != 0) { nonzero = true; break; }
  assert(nonzero);   // silence would hide a stopped stream
  std::printf("source:   %d Hz, 441-frame chunk (20ms), non-silent\n", tone.SampleRate());

  std::printf("PASS\n");
  return 0;
}
