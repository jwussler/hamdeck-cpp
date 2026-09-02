// PTT auto-record, and the sidecar that makes a recording identifiable.
//
// ⚠️ THE POINT OF THIS FILE IS THAT NONE OF IT CAN BE CHECKED BY LOOKING. A
// recording that starts on a tune, or stops on the wrong edge, or carries a
// local-time stamp where a log expects UTC, produces a .wav that plays
// perfectly and matches the wrong contact - or none. Every assertion here is
// against what landed on disk.

#include "check.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/qso_record.h"
#include "../src/recorder.h"

namespace fs = std::filesystem;

namespace {

// A controllable clock, so a 60-second idle timeout costs no wall time. A test
// that has to sleep for the real timeout gets shortened until it stops testing
// the thing it was written for.
std::chrono::steady_clock::time_point g_now = std::chrono::steady_clock::now();
void Advance(int seconds) { g_now += std::chrono::seconds(seconds); }

std::vector<int16_t> Tone(size_t frames) { return std::vector<int16_t>(frames, 1000); }

std::vector<fs::path> Wavs(const fs::path& dir) {
  std::vector<fs::path> out;
  for (const auto& e : fs::directory_iterator(dir))
    if (e.path().extension() == ".wav") out.push_back(e.path());
  return out;
}

std::string Read(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Deliberately crude: no JSON library is linked here, and a real parser would
// hide a malformed sidecar behind a helpful error. This asserts on the bytes.
bool Has(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

std::string Field(const std::string& json, const std::string& key) {
  const auto k = "\"" + key + "\": ";
  const auto at = json.find(k);
  if (at == std::string::npos) return "";
  auto from = at + k.size();
  if (json[from] == '"') {
    ++from;
    return json.substr(from, json.find('"', from) - from);
  }
  const auto end = json.find_first_of(",\n", from);
  return json.substr(from, end - from);
}

}  // namespace

int main() {
  // ⚠️ THIS TEST IS BLIND ON A UTC BOX, AND THE BUILD BOX IS ONE. With TZ=UTC,
  // localtime_r and gmtime_r return the same thing, so the assertion that the
  // sidecar is UTC passes just as happily against a local-time stamp. Verified:
  // swapping gmtime_r for localtime_r in recorder.cpp did NOT fail this file
  // until this line existed.
  //
  // A POSIX TZ string rather than "America/Chicago" on purpose: glibc parses it
  // with no zoneinfo file, so the test keeps its teeth on a bare container that
  // ships no tzdata instead of silently falling back to UTC.
  setenv("TZ", "CST6CDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  {
    const std::time_t t = std::time(nullptr);
    std::tm l{}, g{};
    localtime_r(&t, &l);
    gmtime_r(&t, &g);
    // If these agree the test cannot tell UTC from local, and every timezone
    // assertion below is decoration. Fail loudly rather than pass emptily.
    CHECK(timegm(&l) != timegm(&g));
  }

  const fs::path dir = fs::temp_directory_path() / "hamdeck-qso-test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  const int rate = 8000;
  auto make = [&](QsoRecorder::Options opts) {
    return opts;
  };

  // ── A tune must not start a recording ─────────────────────────────────────
  {
    Recorder rec(dir.string(), rate, 5, 0, 0);
    CHECK(rec.available());
    QsoRecorder q(&rec, make({true, 60, 10000}), [] { return g_now; });
    q.Observe(true, 7185000, "LSB", true, /*tuning=*/true);
    rec.Feed(Tone(rate).data(), rate);
    q.Observe(true, 7185000, "LSB", false, /*tuning=*/true);
    CHECK(!q.active());
    CHECK(!rec.recording());
    CHECK(Wavs(dir).empty());
    std::cout << "  ok   a tune keys the rig and starts nothing\n";
  }

  // ── PTT starts it; a later over pushes the deadline out ───────────────────
  {
    fs::remove_all(dir); fs::create_directories(dir);
    Recorder rec(dir.string(), rate, 5, 0, 0);
    QsoRecorder q(&rec, make({true, 60, 10000}), [] { return g_now; });

    q.Observe(true, 7185000, "LSB", true, false);
    CHECK(q.active());
    CHECK(rec.recording());
    rec.Feed(Tone(rate).data(), rate);
    q.Observe(true, 7185000, "LSB", false, false);

    // 50 s of listening: not idle yet.
    Advance(50);
    q.Observe(true, 7185000, "LSB", false, false);
    CHECK(q.active());

    // A second over resets the clock, so 50 s more must still not end it.
    q.Observe(true, 7185000, "LSB", true, false);
    q.Observe(true, 7185000, "LSB", false, false);
    Advance(50);
    q.Observe(true, 7185000, "LSB", false, false);
    CHECK(q.active());
    std::cout << "  ok   each over pushes the idle deadline out\n";

    // Now let it go quiet past the timeout.
    Advance(20);
    q.Observe(true, 7185000, "LSB", false, false);
    CHECK(!q.active());
    CHECK(!rec.recording());
    CHECK(q.last_stop_reason() == "idle");

    const auto wavs = Wavs(dir);
    CHECK(wavs.size() == 1);
    CHECK(wavs[0].filename().string().starts_with("hamdeck-qso-"));

    // ── The sidecar ───────────────────────────────────────────────────────
    const fs::path side = wavs[0].string() + ".json";
    CHECK(fs::exists(side));
    const std::string json = Read(side);

    CHECK(Field(json, "trigger") == "idle");
    CHECK(Field(json, "freq_hz_start") == "7185000");
    CHECK(Field(json, "mode") == "LSB");
    CHECK(Field(json, "rig_connected") == "true");

    // ⚠️ UTC, AND NOT DERIVED FROM THE FILENAME. The name is local time by
    // house style; every log this will be matched against is UTC. A sidecar
    // that quietly carried local time would match a QSO five hours away.
    const std::string started = Field(json, "started_utc");
    CHECK(started.size() == 20);
    CHECK(started.back() == 'Z');
    CHECK(started[10] == 'T');
    // It must actually BE UTC, not local time with a Z stapled on.
    {
      std::tm tm{};
      std::istringstream in(started);
      in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
      CHECK(!in.fail());
      const std::time_t parsed = timegm(&tm);
      const std::time_t now = std::time(nullptr);
      const double off = std::difftime(now, parsed);
      CHECK(off >= 0 && off < 120);
    }
    // The filename is LOCAL time, the sidecar is UTC, and under a non-UTC TZ
    // the two must disagree. This is the assertion that actually catches a
    // sidecar written with localtime_r.
    {
      const std::string name = wavs[0].filename().string();  // hamdeck-qso-MM-DD-YYYY-HHMMSS.wav
      const std::string local_hh = name.substr(name.size() - 10, 2);
      const std::string utc_hh = started.substr(11, 2);
      CHECK(local_hh != utc_hh);
    }
    std::cout << "  ok   the sidecar's start time is real UTC, not a relabelled local clock\n";

    // Both overs are there, and both are closed.
    CHECK(Has(json, "\"overs\": ["));
    size_t overs = 0, at = 0;
    while ((at = json.find("\"start_utc\"", at)) != std::string::npos) { ++overs; ++at; }
    CHECK(overs == 2);
    CHECK(!Has(json, "\"end_utc\":null"));
    std::cout << "  ok   both overs are recorded and closed\n";
  }

  // ── A QSY ends it, and is measured from where the QSO STARTED ─────────────
  {
    fs::remove_all(dir); fs::create_directories(dir);
    Recorder rec(dir.string(), rate, 5, 0, 0);
    QsoRecorder q(&rec, make({true, 60, 10000}), [] { return g_now; });

    q.Observe(true, 7185000, "LSB", true, false);
    q.Observe(true, 7185000, "LSB", false, false);
    CHECK(q.active());

    // Small steps: no single one trips a reading-to-reading test.
    q.Observe(true, 7190000, "LSB", false, false);   // +5 kHz
    CHECK(q.active());
    // ⚠️ EXACTLY at the threshold is NOT a QSY - the test is `moved >
    // threshold`, so 10 kHz from a 10 kHz setting still counts as the same
    // QSO. Asserted because it is the kind of off-by-one nobody notices until
    // a recording splits in two.
    q.Observe(true, 7195000, "LSB", false, false);   // +10 kHz exactly
    CHECK(q.active());
    q.Observe(true, 7200000, "LSB", false, false);   // +15 kHz
    CHECK(!q.active());
    CHECK(q.last_stop_reason() == "qsy");
    const auto wavs = Wavs(dir);
    CHECK(wavs.size() == 1);
    const std::string json = Read(fs::path(wavs[0].string() + ".json"));
    CHECK(Field(json, "trigger") == "qsy");
    // Start and end frequencies are both there, so the move is visible.
    CHECK(Field(json, "freq_hz_start") == "7185000");
    CHECK(Field(json, "freq_hz_end") == "7200000");
    std::cout << "  ok   a QSY in small steps still ends the recording\n";
  }

  // ── Off by default ────────────────────────────────────────────────────────
  {
    fs::remove_all(dir); fs::create_directories(dir);
    Recorder rec(dir.string(), rate, 5, 0, 0);
    QsoRecorder q(&rec, QsoRecorder::Options{}, [] { return g_now; });
    CHECK(QsoRecorder::Options{}.enabled == false);
    q.Observe(true, 7185000, "LSB", true, false);
    q.Observe(true, 7185000, "LSB", false, false);
    CHECK(!q.active());
    CHECK(Wavs(dir).empty());
    std::cout << "  ok   auto-record records nothing until it is turned on\n";
  }

  // ── A replay clip's sidecar is stamped when the AUDIO happened ────────────
  {
    fs::remove_all(dir); fs::create_directories(dir);
    Recorder rec(dir.string(), rate, 30, 0, 0);
    rec.UpdateProvenance(true, 14074000, "USB");
    rec.Feed(Tone(rate * 20).data(), rate * 20);   // 20 s in the ring
    const auto r = rec.SaveReplay();
    CHECK(r.ok);
    const std::string json = Read(fs::path(r.filename + ".json"));
    CHECK(Field(json, "trigger") == "replay");
    CHECK(Field(json, "freq_hz_start") == "14074000");
    // ⚠️ The ring holds what happened BEFORE the press. Stamping it "now" would
    // file 20 s of audio 20 s after the exchange it contains.
    std::tm tm{};
    std::istringstream in(Field(json, "started_utc"));
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    CHECK(!in.fail());
    const double age = std::difftime(std::time(nullptr), timegm(&tm));
    CHECK(age >= 19 && age < 60);
    // An untracked over list must say null, not [] - "not recorded" is not
    // the same claim as "the operator never transmitted".
    CHECK(Has(json, "\"overs\": null"));
    std::cout << "  ok   a replay is stamped when the audio happened, not when it was saved\n";
  }

  fs::remove_all(dir);
  std::cout << "qso_record: all checks passed\n";
  return 0;
}
