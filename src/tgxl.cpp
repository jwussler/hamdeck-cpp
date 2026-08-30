#include "tgxl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <format>
#include <thread>

namespace {

// Connect with a timeout. A blocking connect to a tuner that is switched off
// hangs for the OS default, which is far longer than an operator will wait.
int ConnectWithTimeout(const std::string& host, int port, int timeout_ms,
                       std::string& error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
    error = "cannot resolve " + host;
    return -1;
  }
  int fd = -1;
  for (addrinfo* a = res; a; a = a->ai_next) {
    fd = ::socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK, a->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
    if (errno != EINPROGRESS) {
      ::close(fd);
      fd = -1;
      continue;
    }
    pollfd p{fd, POLLOUT, 0};
    if (::poll(&p, 1, timeout_ms) > 0) {
      int err = 0;
      socklen_t len = sizeof(err);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) break;
    }
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) error = std::format("no answer from {}:{}", host, port);
  return fd;
}

bool SendLine(int fd, const std::string& line) {
  return ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}

}  // namespace

std::string TgxlTuner::Describe() const {
  return configured() ? std::format("tgxl {}:{}", host_, port_)
                      : "tgxl (not configured)";
}

TgxlTuner::~TgxlTuner() {
  // ⚠️ A tuner thread that outlives this object is a thread that may key the
  // radio after the host has decided to stop. Ask it to stop, then JOIN - the
  // unkey lives on the way out of Worker(), so it must be allowed to run.
  Stop();
  if (worker_.joinable()) worker_.join();
}

void TgxlTuner::Stop() { stop_.store(true); }

TgxlTuner::Result TgxlTuner::Tune() {
  Result r;
  if (!configured()) {
    r.action = "unavailable";
    r.message = "TGXL is not configured - set tgxl_host in the config";
    return r;
  }

  std::lock_guard<std::mutex> lock(mu_);

  // ⚠️ A SECOND PRESS IS A STOP. The reference host makes this button a toggle,
  // and an operator watching an unexpected carrier needs one press to end it.
  // Returning "already tuning" would leave them with nothing to press.
  if (active_.load()) {
    stop_.store(true);
    r.ok = true;
    r.tuning = false;
    r.action = "stopped";
    r.message = "stopping the tune";
    return r;
  }

  // The previous run's thread has finished but may not be joined yet.
  if (worker_.joinable()) worker_.join();

  // Claimed BEFORE the thread starts, so a second request arriving immediately
  // sees active_ and takes the stop path rather than starting a second tune -
  // which would mean two workers keying one transmitter.
  active_.store(true);
  stop_.store(false);
  worker_ = std::thread([this] { Worker(); });

  r.ok = true;
  r.tuning = true;
  r.action = "started";
  r.message = std::format("keying {} W CW and tuning", kTunePowerWatts);
  return r;
}

void TgxlTuner::Worker() {
  // ⚠️ EVERY EXIT PATH UNKEYS AND RESTORES. Timeout, refused connection,
  // operator stop, an exception from a callback - all of them come through
  // here. A tuner that leaves the rig keyed at 15 W in CW is worse than one
  // that never tuned, and this is the code that decides which it is.
  int saved_power = 0;
  std::string saved_mode;
  bool changed_state = false;
  bool keyed = false;

  auto restore = [&] {
    if (keyed && rig_.set_ptt) {
      rig_.set_ptt(false);
      std::this_thread::sleep_for(std::chrono::milliseconds(kPttDropMs));
      keyed = false;
    }
    if (changed_state) {
      if (rig_.set_power && saved_power > 0) {
        rig_.set_power(saved_power);
        std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
      }
      if (rig_.set_mode && !saved_mode.empty()) {
        rig_.set_mode(saved_mode);
        std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
      }
      changed_state = false;
    }
  };
  struct Finally {
    std::function<void()> f;
    ~Finally() { f(); }
  } finally{[&] { restore(); active_.store(false); }};

  try {
    // 1 - save what we are about to change, before changing anything.
    if (rig_.get_power) saved_power = rig_.get_power();
    if (rig_.get_mode) saved_mode = rig_.get_mode();

    // 2 - 15 W CW.
    if (rig_.set_power) {
      rig_.set_power(kTunePowerWatts);
      changed_state = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    }
    if (rig_.set_mode) {
      rig_.set_mode("CW");
      changed_state = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    }
    if (stop_.load()) return;

    // 3 - CONNECT BEFORE KEYING. ⚠️ A deliberate divergence from the reference
    // host, which keys first and then connects: with the tuner switched off or
    // unplugged that puts 15 W into the antenna for the full 3 s connect
    // timeout, tuning nothing. Connecting first costs nothing - the tuner only
    // needs the carrier once autotune is sent, which is still the case below -
    // and it means an unreachable tuner produces NO RF at all.
    std::string err;
    const int fd = ConnectWithTimeout(host_, port_, kConnectTimeoutMs, err);
    if (fd < 0) return;              // nothing keyed; restore() puts power/mode back
    struct Closer {
      int fd;
      ~Closer() { ::close(fd); }
    } closer{fd};

    // 4 - key up, and settle before asking the tuner to measure anything.
    if (rig_.set_ptt) {
      rig_.set_ptt(true);
      keyed = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(kPttSettleMs));
    }
    if (stop_.load()) return;

    // 5 - autotune.
    if (!SendLine(fd, "C1|autotune\n")) return;

    const auto start = std::chrono::steady_clock::now();
    auto elapsed_ms = [&start] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - start).count();
    };

    // 6 - watch tuning go 1 then 0. See the header for why both halves matter.
    bool seen_tuning = false;
    std::string buf;
    while (!stop_.load() && elapsed_ms() < kOverallLimitMs) {
      SendLine(fd, "C1|status\n");
      pollfd p{fd, POLLIN, 0};
      const int pr = ::poll(&p, 1, kReadTimeoutMs);
      if (pr > 0) {
        char tmp[512];
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n == 0) break;           // tuner closed the connection
        if (n > 0) buf.append(tmp, n);
      }

      size_t pos;
      bool done = false;
      while ((pos = buf.find('\n')) != std::string::npos) {
        const std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        // The firmware banner ("V1.2.17") carries no state.
        if (line.empty() || line[0] == 'V') continue;
        const auto at = line.find("tuning=");
        if (at == std::string::npos) continue;

        if (line[at + 7] == '1') {
          seen_tuning = true;
          continue;
        }
        // tuning=0 from here down.
        if (seen_tuning && elapsed_ms() >= kIgnoreEarlyMs) {
          done = true;              // the real 1 -> 0
          break;
        }
        if (seen_tuning) {
          // Inside the ignore window: this is the connect burst, not a tune.
          // DISARM and keep waiting for the real one.
          seen_tuning = false;
          continue;
        }
        if (elapsed_ms() > kNoStartGiveUpMs) {
          done = true;              // never started; stop keying regardless
          break;
        }
      }
      if (done) break;
    }
  } catch (const std::exception&) {
    // Falls through to restore(). An exception must not leave the rig keyed.
  }
}
