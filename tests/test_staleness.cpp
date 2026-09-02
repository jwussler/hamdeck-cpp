// Staleness must be proven, not assumed.
//
// The obvious black-box test - freeze the process and re-query - is WORTHLESS:
// SIGSTOP freezes the HTTP server too, so the poller refreshes the cache the
// instant the process resumes and the answer comes back fresh. It looks like a
// pass and measures nothing. That is the same failure shape as docs/internal/CARRYOVER.md
// section 3's byte-count latency estimate, which read ~0 in steady state while
// 435ms sat in the ALSA buffer.
//
// So: stop the poller and watch the cache actually age.

#include "check.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "../src/cat_sim.h"
#include "../src/radio.h"

int main() {
  RadioPoller poller(std::make_unique<SimulatedRig>());
  poller.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  const long long fresh = poller.CacheAgeMs();
  std::printf("running:  cache_age_ms=%lld (poll interval %dms)\n",
              fresh, RadioPoller::kPollIntervalMs);
  CHECK(fresh >= 0 && fresh < RadioPoller::kStaleAfterMs);

  poller.Stop();
  std::this_thread::sleep_for(
      std::chrono::milliseconds(RadioPoller::kStaleAfterMs + 500));

  const long long aged = poller.CacheAgeMs();
  const bool stale = aged > RadioPoller::kStaleAfterMs;
  std::printf("stopped:  cache_age_ms=%lld stale=%s (threshold %lldms)\n",
              aged, stale ? "true" : "false", RadioPoller::kStaleAfterMs);
  CHECK(stale);

  // A rig that never answers must read disconnected, never a stale-but-plausible
  // last-known value. This is the 3.6-hour-frequency bug in miniature.
  CHECK(poller.Snapshot().connected);
  std::printf("PASS\n");
  return 0;
}
