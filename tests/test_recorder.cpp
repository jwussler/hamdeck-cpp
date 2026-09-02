// Recorder tests.
//
// ⚠️ The point of these is docs/internal/CARRYOVER.md section 1: the reference host answers
// "recording":true from a Start() that set the flag false. So every test here
// checks the REPORTED state against what is actually on disk, not against what
// the call returned.

#include "../src/recorder.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void Check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  if (!ok) ++failures;
}

std::vector<int16_t> Tone(size_t frames) {
  std::vector<int16_t> v(frames);
  for (size_t i = 0; i < frames; ++i) v[i] = static_cast<int16_t>((i % 200) * 50);
  return v;
}

// Reads back what the file actually claims, rather than trusting the writer.
struct Wav {
  bool ok = false;
  uint32_t rate = 0, data_bytes = 0, riff_size = 0;
  uint16_t channels = 0, bits = 0;
  size_t file_size = 0;
};

Wav ReadWav(const std::string& path) {
  Wav w;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return w;
  unsigned char h[44];
  if (std::fread(h, 1, 44, f) != 44) { std::fclose(f); return w; }
  std::fseek(f, 0, SEEK_END);
  w.file_size = static_cast<size_t>(std::ftell(f));
  std::fclose(f);
  auto u32 = [&](int o) {
    return static_cast<uint32_t>(h[o]) | (static_cast<uint32_t>(h[o + 1]) << 8) |
           (static_cast<uint32_t>(h[o + 2]) << 16) |
           (static_cast<uint32_t>(h[o + 3]) << 24);
  };
  auto u16 = [&](int o) {
    return static_cast<uint16_t>(h[o] | (h[o + 1] << 8));
  };
  if (std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0)
    return w;
  w.riff_size = u32(4);
  w.channels = u16(22);
  w.rate = u32(24);
  w.bits = u16(34);
  w.data_bytes = u32(40);
  w.ok = true;
  return w;
}

std::string TempDir() {
  std::string d = (fs::temp_directory_path() / "hamdeck-rec-test").string();
  fs::remove_all(d);
  return d;
}

}  // namespace

int main() {
  std::cout << "recorder\n";

  // --- a directory it cannot write is reported, not pretended away ---------
  {
    Recorder r("/proc/definitely-not-writable", 22050, 5, 60, 30);
    Check(!r.available(), "unwritable directory -> available() is false");
    Check(!r.unavailable_reason().empty(), "and it says why");
    auto res = r.Start();
    Check(!res.ok, "Start() on an unavailable recorder REFUSES");
    Check(!r.recording(), "and recording() stays false");
  }

  // --- empty path means off, and that is not an error ----------------------
  {
    Recorder r("", 22050, 5, 60, 30);
    Check(!r.available(), "empty record_path -> off");
    Check(!r.Start().ok, "Start() refuses when off");
  }

  const std::string dir = TempDir();

  // --- the ring buffer holds only its window -------------------------------
  {
    Recorder r(dir, 8000, 2, 60, 30);          // 2 s ring at 8 kHz = 16000
    Check(r.available(), "writable directory -> available()");
    Check(r.buffering(), "buffering() is on as soon as it is available");

    auto t = Tone(8000 * 5);                    // feed 5 s into a 2 s ring
    r.Feed(t.data(), t.size());
    Check(r.frames_fed() == 8000 * 5, "frames_fed() counts everything fed");

    auto res = r.SaveReplay();
    Check(res.ok, "SaveReplay() writes a file");
    Wav w = ReadWav(res.filename);
    Check(w.ok, "replay is a valid RIFF/WAVE");
    Check(w.rate == 8000 && w.channels == 1 && w.bits == 16,
          "replay header is 8000 Hz / mono / 16-bit");
    // ⚠️ The ring must have DROPPED the oldest 3 s, not grown to hold 5.
    Check(w.data_bytes == 16000 * 2,
          "replay holds exactly the 2 s window, not all 5 s fed");
    Check(w.file_size == 44 + w.data_bytes, "and the file is that size on disk");
    Check(w.riff_size == w.data_bytes + 36, "RIFF size field agrees");
  }

  // --- recording to a file, and the header rewritten on close --------------
  {
    Recorder r(dir, 22050, 10, 60, 30);
    Check(!r.recording(), "starts idle");

    auto start = r.Start();
    Check(start.ok, "Start() succeeds");
    // ⚠️ THE reference bug. recording() must agree with the answer given.
    Check(r.recording(), "recording() is TRUE after a successful Start()");
    Check(!start.filename.empty() && fs::exists(start.filename),
          "the named file exists on disk");

    auto t = Tone(22050 * 3);
    r.Feed(t.data(), t.size());
    Check(r.recorded_seconds() == 3, "recorded_seconds() tracks what was fed");

    auto stop = r.Stop();
    Check(stop.ok, "Stop() succeeds");
    Check(!r.recording(), "recording() is false after Stop()");
    Check(stop.filename == start.filename, "Stop() names the same file");

    Wav w = ReadWav(start.filename);
    Check(w.ok, "recording is a valid RIFF/WAVE");
    Check(w.data_bytes == 22050 * 3 * 2,
          "header length was rewritten to the REAL length on close");
    Check(w.file_size == 44 + w.data_bytes, "file size matches the header");

    Check(!r.Stop().ok, "a second Stop() is refused, not a silent success");
  }

  // --- a second Start() while recording does not orphan the first file -----
  {
    Recorder r(dir, 22050, 10, 60, 30);
    auto a = r.Start();
    Check(a.ok, "Start()");
    auto b = r.Start();
    Check(!b.ok, "Start() while already recording is REFUSED");
    auto t = Tone(22050);
    r.Feed(t.data(), t.size());
    r.Stop();
    Wav w = ReadWav(a.filename);
    Check(w.data_bytes == 22050 * 2, "the original file kept its audio");
  }

  // --- the hard ceiling stops it on its own --------------------------------
  {
    // ⚠️ record_max_seconds exists so a forgotten recording cannot fill the
    // disk of the machine that is also running the transmitter.
    Recorder r(dir, 8000, 5, 2, 1);            // 2 s ceiling
    auto s = r.Start();
    Check(s.ok, "Start() with a 2 s ceiling");
    auto t = Tone(8000 * 4);                    // feed 4 s
    r.Feed(t.data(), t.size());
    Check(!r.recording(), "recorder stopped ITSELF at the ceiling");
    Wav w = ReadWav(s.filename);
    Check(w.data_bytes <= 8000 * 2 * 2 + 2,
          "and the file is capped at the ceiling, not 4 s long");
    Check(w.file_size == 44 + w.data_bytes, "capped file header is still right");
  }

  // --- feeding while not recording only fills the ring ---------------------
  {
    Recorder r(dir, 8000, 5, 60, 30);
    auto t = Tone(8000);
    r.Feed(t.data(), t.size());
    Check(!r.recording(), "Feed() alone never starts a recording");
    size_t before = 0;
    for (auto& e : fs::directory_iterator(dir)) { (void)e; ++before; }
    r.Feed(t.data(), t.size());
    size_t after = 0;
    for (auto& e : fs::directory_iterator(dir)) { (void)e; ++after; }
    Check(before == after, "and it writes no file of its own");
  }

  fs::remove_all(dir);
  std::cout << (failures ? "FAILURES: " : "all passed (")
            << (failures ? std::to_string(failures) : std::string("recorder"))
            << (failures ? "" : ")") << "\n";
  return failures ? 1 : 0;
}
