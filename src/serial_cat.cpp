#include "serial_cat.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>

namespace {

speed_t BaudConstant(int baud) {
  switch (baud) {
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return 0;   // refuse rather than silently pick something
  }
}

}  // namespace

SerialCat::~SerialCat() { Close(); }

void SerialCat::Close() {
  if (fd_ >= 0) {
    flock(fd_, LOCK_UN);
    ::close(fd_);
    fd_ = -1;
  }
}

bool SerialCat::ConfigurePort(int baud) {
  const speed_t speed = BaudConstant(baud);
  if (speed == 0) {
    std::cerr << "serial: unsupported baud " << baud << ", refusing to guess\n";
    return false;
  }
  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) return false;

  cfmakeraw(&tty);            // no echo, no line discipline: CAT is not text
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CSTOPB;     // 8N1
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CRTSCTS;    // no hardware flow control

  // Reads are driven by poll(), so the descriptor itself never blocks.
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;
  return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

bool SerialCat::Open(const std::string& device, int baud) {
  Close();
  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) return false;

  // ⚠️ EXCLUSIVE, and it must fail closed. The .NET host and this one both want
  // the same port; two processes interleaving commands on one CAT link produce
  // replies attributed to the wrong command, which looks like a radio fault and
  // is nearly impossible to diagnose from either side.
  if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
    std::cerr << "serial: " << device << " is already locked by another process"
              << " - refusing to share the CAT link\n";
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  if (!ConfigurePort(baud)) {
    Close();
    return false;
  }
  tcflush(fd_, TCIOFLUSH);   // discard anything stale from a previous session
  device_ = device;

  // ⚠️ PROBE EVEN WHEN THE DEVICE WAS NAMED EXPLICITLY.
  // Opening a serial port proves a port exists, not that a radio is on the end
  // of it. Without this, a host pointed at the wrong port of a dual-UART bridge
  // comes up reporting success and then reads nothing, and the operator is left
  // wondering why the panel is empty.
  //
  // ID; is the only safe probe - it reads the model and changes nothing.
  // A silent port is REPORTED, not fatal: the rig may simply be switched off,
  // and the poller already reports rig_connected=false for that.
  if (const auto id = Exchange("ID;"); id && id->rfind("ID", 0) == 0) {
    id_ = *id;
    if (*id != kExpectedId) {
      std::cerr << "serial: " << device << " answered " << *id << ", expected "
                << kExpectedId << " - check the rig model\n";
    }
  } else {
    std::cerr << "serial: " << device
              << " opened but nothing answered ID; - is the radio on, and is "
                 "this the CAT port of the bridge?\n";
  }
  return true;
}

bool SerialCat::Send(const std::string& cmd) {
  if (fd_ < 0) return false;
  const ssize_t n = ::write(fd_, cmd.data(), cmd.size());
  return n == static_cast<ssize_t>(cmd.size());
}

std::optional<std::string> SerialCat::Exchange(const std::string& cmd) {
  if (fd_ < 0) return std::nullopt;

  // Drop anything unread before asking. A leftover reply from a previous,
  // timed-out command would otherwise be returned as the answer to this one -
  // every subsequent reply off by one, each individually plausible.
  tcflush(fd_, TCIFLUSH);

  if (!Send(cmd)) return std::nullopt;

  // The command's leading letters, used to recognise its reply.
  std::string verb;
  for (char c : cmd) {
    if (c >= 'A' && c <= 'Z') verb += c; else break;
  }

  std::string reply;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kReplyTimeoutMs);

  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    pollfd pfd{fd_, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, static_cast<int>(remaining));
    if (pr < 0) {
      if (errno == EINTR) continue;
      return std::nullopt;
    }
    if (pr == 0) break;   // timed out

    char buf[256];
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) continue;
    reply.append(buf, n);

    // CAT replies are terminated by ';'.
    //
    // ⚠️ VERIFY THE REPLY BELONGS TO THE COMMAND. Observed on the real radio:
    // asking ID; came back "VS0;ID0682;" - a leftover reply from an earlier
    // command sitting in front of the one we wanted. Flushing before the write
    // helps but cannot win a race against a reply already in flight.
    //
    // Returning the first terminated reply blindly would hand back VS0; as the
    // answer to ID;, and then every later reply is off by one - each one
    // individually plausible. A frequency that is really the mode.
    //
    // The verb is the leading letters of the command, so a reply that does not
    // start with them is somebody else's and gets discarded.
    while (true) {
      const auto pos = reply.find(';');
      if (pos == std::string::npos) break;
      const std::string candidate = reply.substr(0, pos + 1);
      if (candidate.rfind(verb, 0) == 0) return candidate;
      reply.erase(0, pos + 1);   // stale reply - drop it and keep reading
    }
    if (reply.size() > 4096) return std::nullopt;   // runaway peer
  }
  // No reply. Say so. A default here would be indistinguishable from a real
  // reading and would poison the cache.
  return std::nullopt;
}

bool SerialCat::OpenFirstResponding(const std::vector<std::string>& candidates, int baud) {
  for (const auto& dev : candidates) {
    if (!Open(dev, baud)) continue;

    // ID; is the only safe probe - it reads the model and changes nothing.
    const auto id = Exchange("ID;");
    if (id && id->rfind("ID", 0) == 0) {
      id_ = *id;
      if (*id != kExpectedId) {
        // Answering but not the expected model: report it rather than pretending
        // it is the right radio or silently refusing.
        std::cerr << "serial: " << dev << " answered " << *id << ", expected "
                  << kExpectedId << " - using it anyway, check the rig model\n";
      }
      return true;
    }
    Close();
  }
  return false;
}

std::string SerialCat::Describe() const {
  if (fd_ < 0) return "serial (closed)";
  return std::format("serial {} id={}", device_, id_.empty() ? "?" : id_);
}
