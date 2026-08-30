#include "amp_tuner.h"

#include <chrono>
#include <format>

AmpTuner::~AmpTuner() {
  Stop();
  if (worker_.joinable()) worker_.join();
}

void AmpTuner::Stop() { stop_.store(true); }

AmpTuner::Result AmpTuner::Tune() {
  Result r;
  std::lock_guard<std::mutex> lock(mu_);

  // A second press stops the carrier. Ten seconds is long enough that an
  // operator who changes their mind needs a way out that is not the watchdog.
  if (active_.load()) {
    stop_.store(true);
    r.ok = true;
    r.action = "stopped";
    r.message = "stopping the carrier";
    return r;
  }
  if (worker_.joinable()) worker_.join();

  active_.store(true);
  stop_.store(false);
  worker_ = std::thread([this] { Worker(); });

  r.ok = true;
  r.tuning = true;
  r.action = "started";
  r.message = std::format("{} W CW carrier for {} s, then {} W", kTunePowerWatts,
                          kCarrierMs / 1000, kAfterPowerWatts);
  return r;
}

void AmpTuner::Worker() {
  bool keyed = false;
  std::string saved_mode;

  auto unkey = [&] {
    if (keyed && rig_.set_ptt) {
      rig_.set_ptt(false);
      std::this_thread::sleep_for(std::chrono::milliseconds(kPttDropMs));
      keyed = false;
    }
  };
  struct Finally {
    std::function<void()> f;
    ~Finally() { f(); }
  } finally{[&] {
    unkey();
    // ⚠️ 100 W EVEN ON THE WAY OUT OF A FAILURE. The reference host forces it
    // in its exception path too: leaving the rig at the 20 W tune power after
    // an amp tune means the next over goes out at a power nobody chose.
    if (rig_.set_power) rig_.set_power(kAfterPowerWatts);
    if (!saved_mode.empty() && rig_.set_mode) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
      rig_.set_mode(saved_mode);
    }
    active_.store(false);
  }};

  try {
    if (rig_.get_mode) saved_mode = rig_.get_mode();

    if (rig_.set_power) {
      rig_.set_power(kTunePowerWatts);
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    }
    if (rig_.set_mode) {
      rig_.set_mode("CW");
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    }
    if (stop_.load()) return;

    if (rig_.set_ptt) {
      rig_.set_ptt(true);
      keyed = true;
    }
    // Checked every 100 ms rather than one long sleep, so a stop lands quickly.
    for (int elapsed = 0; elapsed < kCarrierMs && !stop_.load(); elapsed += 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception&) {
    // finally{} unkeys and sets 100 W.
  }
}
