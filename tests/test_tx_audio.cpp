// TX audio receive path.
//
// This is the path that puts a human voice on the air, so the assertions are
// about not transmitting the wrong thing.

#include "check.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../src/tx_audio.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  auto sink = std::make_unique<NullTxSink>(48000);
  NullTxSink* raw = sink.get();
  TxAudioReceiver rx(std::move(sink));

  // ── One transmitter at a time ─────────────────────────────────────────────
  CHECK(rx.Claim("joe"));
  CHECK(!rx.Claim("someone-else"));      // must not evict, must not queue
  CHECK(rx.HeldBy("joe"));
  CHECK(rx.Holder() == "joe");
  std::printf("exclusive: second client refused while joe holds it\n");

  rx.Release("someone-else");             // a non-holder cannot release it
  CHECK(rx.HeldBy("joe"));
  std::printf("exclusive: a non-holder cannot release the claim\n");

  // ── Framing ───────────────────────────────────────────────────────────────
  const std::vector<int16_t> frame(960, 1234);   // 20ms at 48k
  const char* bytes = reinterpret_cast<const char*>(frame.data());
  CHECK(rx.Accept(bytes, frame.size() * 2, /*keyed=*/true));
  CHECK(rx.Accepted() == 1);

  // ⚠️ An odd byte count means the framing is wrong. Interpreting it anyway
  // shifts every following sample by one byte, which is loud noise on the air,
  // not a subtle glitch.
  CHECK(!rx.Accept(bytes, 961, /*keyed=*/true));
  CHECK(!rx.Accept(bytes, 0, /*keyed=*/true));
  CHECK(rx.Accepted() == 1);             // neither was counted
  std::printf("framing:   odd byte count and empty frame both refused\n");

  // ── Trim only between overs ───────────────────────────────────────────────
  // While KEYED the queue is allowed to run past its bound: dropping audio
  // mid-transmission removes a syllable from someone's sentence.
  for (int i = 0; i < 60; ++i) rx.Accept(bytes, frame.size() * 2, /*keyed=*/true);
  CHECK(rx.Dropped() == 0);
  CHECK(rx.QueueDepth() > TxAudioReceiver::kMaxQueuedChunks);
  std::printf("keyed:     queue ran to %zu chunks with ZERO drops (bound is %zu)\n",
              rx.QueueDepth(), TxAudioReceiver::kMaxQueuedChunks);

  // Unkeyed, the backlog is trimmed so the next over starts at the target depth.
  for (int i = 0; i < 20; ++i) rx.Accept(bytes, frame.size() * 2, /*keyed=*/false);
  CHECK(rx.Dropped() > 0);
  std::printf("unkeyed:   trimmed between overs, %zu dropped\n", rx.Dropped());

  // ── Draining reaches the sink ─────────────────────────────────────────────
  const size_t before = raw->FramesWritten();
  const size_t pumped = rx.Pump();
  CHECK(pumped > 0);
  CHECK(raw->FramesWritten() > before);
  std::printf("pump:      %zu chunks reached the sink (%zu frames)\n",
              pumped, raw->FramesWritten());

  // ── Releasing must not leak one operator's audio into another's over ──────
  rx.Accept(bytes, frame.size() * 2, true);
  CHECK(rx.QueueDepth() > 0);
  rx.Release("joe");
  CHECK(rx.QueueDepth() == 0);
  CHECK(rx.Holder().empty());
  std::printf("release:   queue cleared, so no audio carries into the next over\n");

  CHECK(rx.Claim("someone-else"));       // now free
  std::printf("handover:  next client can claim it\n");

  // The null sink must never claim to be a radio.
  CHECK(rx.Backend().find("null") != std::string::npos);
  std::printf("honesty:   backend reports '%s'\n", rx.Backend().c_str());

  std::printf("PASS\n");
  return 0;
}
