#include "radio.h"

#include <iostream>

std::string ModeName(int code) {
  switch (code) {
    case 1:  return "LSB";
    case 2:  return "USB";
    case 3:  return "CW";
    case 4:  return "FM";
    case 5:  return "AM";
    case 6:  return "RTTY-L";
    case 7:  return "CW-R";
    case 8:  return "DATA-L";
    case 9:  return "RTTY-U";
    default: return "";
  }
}

RadioPoller::RadioPoller(std::unique_ptr<CatTransport> cat) : cat_(std::move(cat)) {}

RadioPoller::~RadioPoller() { Stop(); }

void RadioPoller::Start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this] { PollLoop(); });
}

void RadioPoller::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void RadioPoller::Enqueue(const std::string& cat_command) {
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    queue_.push_back(cat_command);
  }
  // ⚠️ Any command may have moved a slow-polled field, so force a full re-read on
  // the next cycle instead of waiting up to a second for the scheduled one.
  // Without this, toggling the noise blanker leaves /api/status/full reporting
  // the old value for up to a second and the panel appears not to have responded
  // - so the operator presses it again, and now it really is off.
  //
  // This re-READS the rig; it does not assume the command worked. An optimistic
  // local update would be a lie whenever the rig rejected the command.
  full_dirty_.store(true);
}

void RadioPoller::EnqueueTask(std::function<void(CatTransport&)> task) {
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    tasks_.push_back(std::move(task));
  }
  full_dirty_.store(true);
}

void RadioPoller::DrainQueue() {
  // Tasks first: a compound sequence should not have a loose command land in
  // the middle of it.
  for (;;) {
    std::function<void(CatTransport&)> task;
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (tasks_.empty()) break;
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    task(*cat_);
  }
  for (;;) {
    std::string cmd;
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (queue_.empty()) return;
      cmd = queue_.front();
      queue_.pop_front();
    }
    cat_->Send(cmd);   // on the poller thread, where serial access belongs
  }
}

// Runs every poll cycle, on the thread that owns the serial port, so it can key
// down without waiting for anything. Granularity is the poll interval (200ms),
// which is far finer than a 180s timeout needs.
void RadioPoller::CheckWatchdog(bool tx_now) {
  const int limit = ptt_timeout_s_.load();
  const auto now = std::chrono::steady_clock::now();

  if (tx_now && !was_tx_) keyed_since_ = now;   // rising edge
  was_tx_ = tx_now;
  if (!tx_now || limit <= 0) return;

  const double held =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - keyed_since_).count() / 1000.0;
  if (held < limit) return;

  // Drop PTT directly rather than queueing it. Nothing is worth an open carrier,
  // and a queued command waits behind whatever else is pending.
  cat_->Send("TX0;");
  watchdog_trips_.fetch_add(1);
  was_tx_ = false;
  if (watchdog_cb_) watchdog_cb_(held);
}

void RadioPoller::PollLoop() {
  while (running_.load()) {
    DrainQueue();
    PollOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  }
}

void RadioPoller::PollOnce() {
  RigSnapshot s;

  // A failed exchange leaves the field at its default and connected=false. It
  // must never fall back to the previous value: a stale number presented as
  // current is exactly the 3.6-hour-frequency bug.
  auto id = cat_->Exchange("ID;");
  if (!id.has_value()) {
    std::lock_guard<std::mutex> lock(mu_);
    snap_ = s;                                  // connected = false
    snap_.taken = std::chrono::steady_clock::now();
    return;
  }
  s.connected = true;

  if (auto r = cat_->Exchange("FA;");  r && r->size() >= 12) s.freq   = std::stoll(r->substr(2, 9));
  if (auto r = cat_->Exchange("FB;");  r && r->size() >= 12) s.freq_b = std::stoll(r->substr(2, 9));
  if (auto r = cat_->Exchange("MD0;"); r && r->size() >= 5)  s.mode   = ModeName(r->at(3) - '0');
  if (auto r = cat_->Exchange("PC;");  r && r->size() >= 6)  s.power  = std::stoi(r->substr(2, 3));
  if (auto r = cat_->Exchange("TX;");  r && r->size() >= 4)  s.tx     = r->at(2) != '0';
  if (auto r = cat_->Exchange("ST;");  r && r->size() >= 4)  s.split  = r->at(2) != '0';
  if (auto r = cat_->Exchange("VS;");  r && r->size() >= 4)  s.vfo    = (r->at(2) != '0') ? "B" : "A";
  if (auto r = cat_->Exchange("LK;");  r && r->size() >= 4)  s.vfo_locked = r->at(2) != '0';

  PollMeters(s);

  // Carry the slow-moving fields forward on cycles where they are not re-read,
  // so /api/status/full does not flicker between real values and defaults.
  if (cycle_ % kFullEveryNCycles == 0 || full_dirty_.exchange(false)) {
    PollFull(s);
  } else {
    std::lock_guard<std::mutex> lock(mu_);
    s.ant = snap_.ant; s.rxant = snap_.rxant; s.nb = snap_.nb; s.nr = snap_.nr;
    s.notch = snap_.notch; s.preamp = snap_.preamp; s.att = snap_.att;
    s.agc = snap_.agc; s.vox = snap_.vox; s.comp = snap_.comp; s.mon = snap_.mon;
    s.rit = snap_.rit; s.rit_offset = snap_.rit_offset; s.xit = snap_.xit;
    s.rf_gain = snap_.rf_gain; s.cw_speed = snap_.cw_speed; s.width_idx = snap_.width_idx;
    s.af_gain = snap_.af_gain; s.sub_af_gain = snap_.sub_af_gain;
  }
  ++cycle_;

  s.taken = std::chrono::steady_clock::now();
  CheckWatchdog(s.tx);
  std::lock_guard<std::mutex> lock(mu_);
  snap_ = s;
}

int RadioPoller::TransmitSecondsRemaining() const {
  const int limit = ptt_timeout_s_.load();
  if (limit <= 0) return 0;
  std::lock_guard<std::mutex> lock(mu_);
  if (!snap_.tx) return 0;
  const auto held = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - keyed_since_).count();
  const long long left = limit - held;
  return left > 0 ? static_cast<int>(left) : 0;
}

RigSnapshot RadioPoller::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snap_;
}

long long RadioPoller::CacheAgeMs() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (snap_.taken.time_since_epoch().count() == 0) return -1;  // never polled
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - snap_.taken).count();
}

namespace {
// A CAT reply is fixed-width. Reading the wrong slice yields a plausible number
// from the wrong field, which is worse than no number at all - so every read
// checks the length first and leaves the default in place if it is short.
bool Digits(const std::optional<std::string>& r, size_t at, size_t n, int& out) {
  if (!r || r->size() < at + n) return false;
  try {
    out = std::stoi(r->substr(at, n));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}
bool Flag(const std::optional<std::string>& r, size_t at, bool& out) {
  if (!r || r->size() <= at) return false;
  out = r->at(at) != '0';
  return true;
}
}  // namespace

void RadioPoller::PollMeters(RigSnapshot& s) {
  int v = 0;
  if (Digits(cat_->Exchange("SM0;"), 3, 3, v)) s.s_meter = v;
  if (Digits(cat_->Exchange("RM6;"), 3, 3, v)) s.swr = v;
  if (Digits(cat_->Exchange("RM4;"), 3, 3, v)) s.alc = v;
  if (Digits(cat_->Exchange("RM5;"), 3, 3, v)) s.power_mtr = v;
}

void RadioPoller::PollFull(RigSnapshot& s) {
  int v = 0;
  bool b = false;
  if (Digits(cat_->Exchange("AN0;"), 3, 1, v)) s.ant = v;
  if (Flag(cat_->Exchange("NB0;"), 3, b))  s.nb = b;
  if (Flag(cat_->Exchange("NR0;"), 3, b))  s.nr = b;
  if (Flag(cat_->Exchange("BC0;"), 3, b))  s.notch = b;
  if (Digits(cat_->Exchange("PA0;"), 3, 1, v)) s.preamp = v;
  if (Flag(cat_->Exchange("RA0;"), 3, b))  s.att = b;
  if (Flag(cat_->Exchange("VX;"),  2, b))  s.vox = b;
  if (Flag(cat_->Exchange("PR0;"), 3, b))  s.comp = b;
  if (Digits(cat_->Exchange("ML0;"), 3, 3, v)) s.mon = v > 0;   // ML0PPP, 000=off
  if (Flag(cat_->Exchange("RT;"),  2, b))  s.rit = b;
  if (Flag(cat_->Exchange("XT;"),  2, b))  s.xit = b;
  if (Digits(cat_->Exchange("RG0;"), 3, 3, v)) s.rf_gain = v;
  if (Digits(cat_->Exchange("KS;"),  2, 3, v)) s.cw_speed = v;
  if (Digits(cat_->Exchange("AG0;"), 3, 3, v)) s.af_gain = v;
  if (Digits(cat_->Exchange("AG1;"), 3, 3, v)) s.sub_af_gain = v;
  if (Flag(cat_->Exchange("EX030103;"), 8, b)) s.rxant = b;   // ANT3 SELECT menu item
  if (Digits(cat_->Exchange("SH0;"), 4, 2, v)) s.width_idx = v;   // SH00<nn>
  if (auto r = cat_->Exchange("GT0;"); r && r->size() >= 5) {
    switch (r->at(3)) {
      case '0': s.agc = "OFF";  break;
      case '1': s.agc = "FAST"; break;
      case '2': s.agc = "MID";  break;
      case '3': s.agc = "SLOW"; break;
      case '4': s.agc = "AUTO"; break;
      default:  break;   // unknown code: keep the previous value, never guess
    }
  }
}
