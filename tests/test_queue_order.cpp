// Read-after-write ordering on the poller queue.
//
// ⚠️ THIS BUG WAS SILENT, GENERAL, AND LOOKED LIKE A SLOW RADIO.
//
// Commands and tasks used to live in two deques, with tasks always drained first. So a
// read submitted AFTER a write still ran BEFORE it, and every read-after-write returned
// the state from before the write - on any route, not just the one that found it. On the
// panel that reads as "the radio has not responded yet", so the operator presses the
// button again, which is the worst possible response to a stale read.
//
// Found via /api/cw/memory/3 followed by /api/cw/status reporting not-playing, every time.

#include "check.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

#include "../src/cat_sim.h"
#include "../src/radio.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  auto sim = std::make_unique<SimulatedRig>();
  SimulatedRig* raw = sim.get();
  RadioPoller poller(std::move(sim));
  poller.Start();

  // ── A write, then a read submitted after it ─────────────────────────────
  // KY8; is CW memory 3 on this radio. The read must observe it.
  poller.Enqueue("KY8;");
  auto p1 = std::make_shared<std::promise<std::string>>();
  auto f1 = p1->get_future();
  poller.EnqueueTask([p1](CatTransport& cat) {
    p1->set_value(cat.Exchange("KY;").value_or("<none>"));
  });
  CHECK(f1.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  const std::string after_play = f1.get();
  if (after_play != "KY1;") std::fprintf(stderr, "got %s, wanted KY1;\n", after_play.c_str());
  CHECK(after_play == "KY1;");
  std::printf("write->read: a read submitted after a write observes it (%s)\n",
              after_play.c_str());

  // ── And the reverse, so this is ordering rather than a constant ──────────
  poller.Enqueue("KY0;");
  auto p2 = std::make_shared<std::promise<std::string>>();
  auto f2 = p2->get_future();
  poller.EnqueueTask([p2](CatTransport& cat) {
    p2->set_value(cat.Exchange("KY;").value_or("<none>"));
  });
  CHECK(f2.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  CHECK(f2.get() == "KY0;");
  std::printf("stop->read:  the stop is observed too, so it is order not a constant\n");

  // ── A longer interleaving, to catch a queue that only happens to work ───
  // Writes and reads alternating: every read must see the write immediately before it.
  for (int i = 0; i < 4; ++i) {
    const bool playing = (i % 2) == 0;
    poller.Enqueue(playing ? "KY7;" : "KY0;");
    auto p = std::make_shared<std::promise<std::string>>();
    auto f = p->get_future();
    poller.EnqueueTask([p](CatTransport& cat) {
      p->set_value(cat.Exchange("KY;").value_or("<none>"));
    });
    CHECK(f.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(f.get() == (playing ? "KY1;" : "KY0;"));
  }
  std::printf("interleave:  4 alternating write/read pairs, each read sees its own write\n");

  // The simulator refuses a channel outside 0 and 6-A, so a wrong mapping cannot be
  // mistaken for a working one here either.
  CHECK(!raw->Send("KY3;"));
  CHECK(raw->Send("KY6;"));
  std::printf("channels:    the simulator refuses KY3; and accepts KY6;\n");

  poller.Stop();
  std::printf("\nqueue-order: all checks passed\n");
  return 0;
}
