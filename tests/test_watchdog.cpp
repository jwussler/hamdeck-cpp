// Transmit watchdog and the command queue.
//
// The watchdog is the safety property in the whole host: if it does not fire,
// a lost link or a slept laptop leaves the rig keyed with nobody watching
// (CARRYOVER.md section 4b). So the test asserts the RADIO actually stopped
// transmitting, read back through CAT - not merely that a trip counter moved.
// A counter is a claim; TX; returning 0 is the outcome.

#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "../src/cat_sim.h"
#include "../src/radio.h"

namespace {
void SettleMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}

int main() {
  auto sim = std::make_unique<SimulatedRig>();
  SimulatedRig* raw = sim.get();
  RadioPoller poller(std::move(sim));

  poller.SetPttTimeoutSeconds(1);
  double tripped_at = 0;
  poller.OnWatchdogTrip([&](double held) { tripped_at = held; });
  poller.Start();
  SettleMs(400);

  // Commands from a "request thread" go through the queue, never the port.
  poller.Enqueue("FA014200000;");
  poller.Enqueue("MD01;");
  SettleMs(600);
  assert(poller.Snapshot().freq == 14200000);
  assert(poller.Snapshot().mode == "LSB");
  std::printf("queue:    commands applied in order (freq=%lld mode=%s)\n",
              poller.Snapshot().freq, poller.Snapshot().mode.c_str());

  // Key up, then leave it keyed past the limit and watch the watchdog take it.
  poller.Enqueue("TX1;");
  SettleMs(400);
  assert(poller.Snapshot().tx);
  std::printf("ptt:      keyed, watchdog limit %ds\n", poller.PttTimeoutSeconds());

  SettleMs(1400);

  // The outcome, straight from the rig - not from our own bookkeeping.
  const auto tx = raw->Exchange("TX;");
  assert(tx.has_value());
  std::printf("watchdog: rig reports %s after %.1fs held, trips=%d\n",
              tx->c_str(), tripped_at, poller.WatchdogTrips());
  assert(*tx == "TX0;");
  assert(poller.WatchdogTrips() == 1);
  assert(tripped_at >= 1.0);
  assert(!poller.Snapshot().tx);

  // Zero disables it: keyed stays keyed.
  poller.SetPttTimeoutSeconds(0);
  poller.Enqueue("TX1;");
  SettleMs(1600);
  assert(poller.Snapshot().tx);
  assert(poller.WatchdogTrips() == 1);
  std::printf("watchdog: 0 disables (still keyed after 1.6s, trips still 1)\n");

  // ── Unkey on shutdown ─────────────────────────────────────────────────────
  // ⚠️ The watchdog lives in THIS PROCESS. If the process exits while the rig is
  // keyed, the watchdog dies with it and nothing is left to drop PTT - the
  // station sits on an open carrier with nothing watching at all. So shutdown
  // must unkey, and must confirm the rig actually stopped rather than assume the
  // command landed.
  poller.SetPttTimeoutSeconds(0);          // watchdog off, so it cannot do the work
  poller.Enqueue("TX1;");
  SettleMs(400);
  assert(poller.Snapshot().tx);
  assert(raw->Exchange("TX;").value() == "TX1;");

  poller.Stop();                            // as on the shutdown path
  const bool confirmed = poller.UnkeyAndConfirm();
  assert(confirmed);
  assert(raw->Exchange("TX;").value() == "TX0;");
  std::printf("shutdown: keyed rig unkeyed and CONFIRMED after the poller stopped\n");

  std::printf("PASS\n");
  return 0;
}
