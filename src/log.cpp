#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace hdlog {
namespace {

std::atomic<int> g_level{kQuiet};
std::mutex g_mu;

const char* Name(int l) {
  switch (l) {
    case kInfo:    return "info ";
    case kVerbose: return "verb ";
    case kTrace:   return "trace";
    default:       return "     ";
  }
}

}  // namespace

void SetLevel(int level) { g_level.store(level); }
int level() { return g_level.load(std::memory_order_relaxed); }

void Line(int want, std::string_view tag, std::string_view msg) {
  if (!On(want)) return;

  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto secs = system_clock::to_time_t(now);
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm{};
  localtime_r(&secs, &tm);
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d",
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));

  // ⚠️ One write under one lock, and flushed. Interleaved fragments from the
  // poller thread and eight request threads are worse than no log at all -
  // you cannot tell which line a fragment belonged to.
  std::lock_guard<std::mutex> lock(g_mu);
  std::fprintf(stdout, "%s %s %-6.*s %.*s\n", stamp, Name(want),
               static_cast<int>(tag.size()), tag.data(),
               static_cast<int>(msg.size()), msg.data());
  std::fflush(stdout);
}

std::string Escape(std::string_view raw) {
  std::string out;
  out.reserve(raw.size() + 8);
  for (unsigned char c : raw) {
    if (c == '\r') out += "\\r";
    else if (c == '\n') out += "\\n";
    else if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\x%02x", c);
      out += buf;
    }
  }
  return out;
}

}  // namespace hdlog
