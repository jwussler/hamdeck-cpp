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

TgxlTuner::Result TgxlTuner::Tune() {
  Result r;
  if (!configured()) {
    r.action = "unavailable";
    r.message = "TGXL is not configured - set tgxl_host in the config";
    return r;
  }

  // ⚠️ Claimed atomically. Two overlapping tunes would key the transmitter twice
  // and fight each other; the second caller is told, not queued.
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (active_.load()) {
      r.ok = true;
      r.tuning = true;
      r.action = "already-tuning";
      r.message = "a tune is already running";
      return r;
    }
    active_.store(true);
  }
  struct Release {
    std::atomic<bool>* f;
    ~Release() { f->store(false); }
  } release{&active_};

  std::string err;
  const int fd = ConnectWithTimeout(host_, port_, kConnectTimeoutMs, err);
  if (fd < 0) {
    r.action = "unavailable";
    r.message = err;
    return r;
  }
  struct Closer {
    int fd;
    ~Closer() { ::close(fd); }
  } closer{fd};

  if (!SendLine(fd, "C1|autotune\n")) {
    r.action = "unavailable";
    r.message = "could not send autotune";
    return r;
  }

  const auto start = std::chrono::steady_clock::now();
  auto elapsed_ms = [&start] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count();
  };

  bool seen_tuning = false;
  std::string buf;
  while (elapsed_ms() < kOverallLimitMs) {
    SendLine(fd, "C1|status\n");
    pollfd p{fd, POLLIN, 0};
    const int pr = ::poll(&p, 1, kReadTimeoutMs);
    if (pr > 0) {
      char tmp[512];
      const ssize_t n = ::read(fd, tmp, sizeof(tmp));
      if (n > 0) buf.append(tmp, n);
    }

    // Consume whole lines, newest state wins.
    size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
      const std::string line = buf.substr(0, pos);
      buf.erase(0, pos + 1);
      const auto at = line.find("tuning=");
      if (at == std::string::npos) continue;
      const bool tuning = line[at + 7] == '1';
      if (tuning) {
        seen_tuning = true;
      } else if (seen_tuning || elapsed_ms() > kIgnoreEarlyMs) {
        // ⚠️ Only trust tuning=0 once tuning=1 has been seen, or the early
        // window has passed. The tuner reports 0 briefly before it starts, and
        // believing that reports a tune that never happened.
        r.ok = true;
        r.tuning = false;
        r.action = "stopped";
        r.message = std::format("tuned in {} ms", elapsed_ms());
        return r;
      }
    }
  }

  r.ok = true;
  r.tuning = true;
  r.action = "timeout";
  r.message = std::format("tuner did not report finished within {} s",
                          kOverallLimitMs / 1000);
  return r;
}
