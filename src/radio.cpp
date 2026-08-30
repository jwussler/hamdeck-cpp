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
  std::lock_guard<std::mutex> lock(queue_mu_);
  queue_.push_back(cat_command);
}

void RadioPoller::DrainQueue() {
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

  s.taken = std::chrono::steady_clock::now();
  CheckWatchdog(s.tx);
  std::lock_guard<std::mutex> lock(mu_);
  snap_ = s;
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
