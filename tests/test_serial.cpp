// The real serial backend, driven through a pty with a fake rig on the far end.
//
// This is the whole point of writing SerialCat before the hardware window: every
// failure mode below - a timeout, a chunked reply, leftover bytes from a previous
// command, two processes fighting over the port - is far cheaper to find here
// than with the station off the air.

#include <pty.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "../src/serial_cat.h"

namespace {

int g_master = -1;
std::atomic<bool> g_run{true};
std::atomic<bool> g_answer_unknown{false};

// A fake FTDX-101MP. Answers only what the real rig answers; an unknown command
// gets silence, which is exactly how the radio behaves and is what the timeout
// path has to cope with.
void FakeRig() {
  std::string in;
  while (g_run.load()) {
    char buf[128];
    const ssize_t n = ::read(g_master, buf, sizeof(buf));
    if (n <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    in.append(buf, n);
    size_t pos;
    while ((pos = in.find(';')) != std::string::npos) {
      const std::string cmd = in.substr(0, pos + 1);
      in.erase(0, pos + 1);
      std::string reply;
      if (cmd == "ID;")       reply = "ID0682;";
      else if (cmd == "FA;")  reply = "FA014074000;";
      else if (cmd == "SPLIT;") {
        // Deliberately dribbled out in pieces to prove reassembly.
        const std::string parts = "ST0;";
        for (char c : parts) {
          ::write(g_master, &c, 1);
          std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        continue;
      } else if (cmd == "TRAIL;") {
        // A reply plus the beginning of another message. Only the first
        // terminated reply may be returned.
        reply = "MD02;GARBAGE-AFTER";
      } else if (g_answer_unknown.load()) {
        reply = "??;";
      }
      if (!reply.empty()) ::write(g_master, reply.data(), reply.size());
    }
  }
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);   // an aborted assert must not eat the log
  int slave = -1;
  char name[256];
  if (openpty(&g_master, &slave, name, nullptr, nullptr) != 0) {
    std::printf("openpty failed - cannot run\n");
    return 1;
  }
  ::close(slave);   // SerialCat opens it by name, like a real device
  const std::string dev = name;
  std::thread rig(FakeRig);

  SerialCat cat;

  // The port is identified by ASKING, not by assuming a device number. The
  // CP2105 is a dual UART, so one physical device gives two ports and only one
  // is CAT; indices also shift when USB devices come and go.
  const bool opened = cat.OpenFirstResponding({"/dev/does-not-exist", dev});
  assert(opened);
  assert(cat.Connected());
  std::printf("probe:    found CAT on the second candidate via ID; -> %s\n",
              cat.Exchange("ID;")->c_str());

  auto fa = cat.Exchange("FA;");
  assert(fa && *fa == "FA014074000;");
  std::printf("read:     FA; -> %s\n", fa->c_str());

  // Chunked reply must be reassembled, not truncated at the first read().
  auto st = cat.Exchange("SPLIT;");
  assert(st && *st == "ST0;");
  std::printf("chunked:  reassembled -> %s\n", st->c_str());

  // Only the first terminated reply is returned; trailing bytes are not leaked.
  auto md = cat.Exchange("TRAIL;");
  assert(md && *md == "MD02;");
  std::printf("trailing: returned only %s, ignored the rest\n", md->c_str());

  // ⚠️ The critical one: a stale reply must NEVER be returned as the answer to a
  // later command. Without the input flush this comes back as the leftover
  // "GARBAGE" and every subsequent reply is off by one, each plausible on its own.
  auto next = cat.Exchange("FA;");
  assert(next && *next == "FA014074000;");
  std::printf("no-slip:  next command still got its own reply\n");

  // An unanswered command must time out, not hang. A wedged read would stall the
  // poller while the API kept serving a cache nobody was refreshing.
  const auto t0 = std::chrono::steady_clock::now();
  auto none = cat.Exchange("ZZ;");
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::printf("timeout:  unanswered command -> %s after %lldms (limit %dms)\n",
              none.has_value() ? none->c_str() : "nullopt",
              static_cast<long long>(ms), SerialCat::kReplyTimeoutMs);
  assert(!none.has_value());
  // Tolerance below the nominal limit: poll() rounds and the deadline is checked
  // at the top of the loop, so a correct 250ms timeout measures ~249ms. The
  // assertion that matters is that it RETURNED and did so promptly - a hang is
  // the failure being guarded against, not a millisecond of rounding.
  assert(ms >= SerialCat::kReplyTimeoutMs - 10);
  assert(ms < SerialCat::kReplyTimeoutMs + 250);

  // Two processes must not share the CAT link.
  SerialCat second;
  const bool second_opened = second.Open(dev);
  assert(!second_opened);
  std::printf("exclusive: a second opener is refused\n");

  // An unsupported baud is refused rather than silently substituted.
  SerialCat odd;
  assert(!odd.Open(dev, 12345));
  std::printf("baud:     unsupported rate refused\n");

  cat.Close();
  assert(!cat.Connected());
  g_run.store(false);
  rig.join();
  ::close(g_master);
  std::printf("PASS\n");
  return 0;
}
