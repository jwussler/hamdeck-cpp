// S-meter calibration.
//
// The point of these assertions is that the table is USED correctly, and that
// the naive assumption it replaces is visibly wrong.

#include <cassert>
#include <cstdio>

#include "../src/rig_cal.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  // Anchors straight from Hamlib's table.
  assert(RawToDb(160) == 0);       // S9
  assert(RawToDb(136) == -6);      // S8
  assert(RawToDb(25) == -48);      // S1
  assert(RawToDb(255) == 60);      // +60
  std::printf("anchors:  raw 160 = 0 dB (S9), 136 = -6 (S8), 255 = +60\n");

  assert(DbToSUnit(0) == "S9");
  assert(DbToSUnit(-6) == "S8");
  assert(DbToSUnit(-54) == "S0");
  assert(DbToSUnit(20) == "S9+20");
  std::printf("units:    0 -> %s, -6 -> %s, -54 -> %s, +20 -> %s\n",
              DbToSUnit(0).c_str(), DbToSUnit(-6).c_str(), DbToSUnit(-54).c_str(),
              DbToSUnit(20).c_str());

  // ⚠️ THE ASSUMPTION THIS REPLACES. The obvious guess is that S9 sits at the
  // middle of a 0-255 range. It does not: raw 128 is about S7, one and a half
  // S-units below S9. That is the difference between "5 by 9" and "5 by 7" in a
  // report passed on to another operator.
  const int mid_db = RawToDb(128);
  const std::string mid = DbToSUnit(mid_db);
  std::printf("midpoint: raw 128 is %d dB = %s, NOT S9 - the naive guess is "
              "~1.5 S-units high\n", mid_db, mid.c_str());
  assert(mid != "S9");
  assert(mid_db < -6);

  // Clamping, matching Hamlib's rig_raw2val().
  assert(RawToDb(-5) == RawToDb(0));
  assert(RawToDb(9999) == RawToDb(255));
  std::printf("clamp:    out-of-range values clamp to the end points\n");

  // Monotonic: a stronger signal must never read weaker.
  int prev = RawToDb(0);
  for (int raw = 1; raw <= 255; ++raw) {
    const int db = RawToDb(raw);
    assert(db >= prev);
    prev = db;
  }
  std::printf("monotonic: every raw step 0..255 is non-decreasing\n");

  // The scale ticks must be inside the range and ascending, or a client draws
  // labels off the end of its meter.
  int last = -1;
  for (const auto& t : SMeterScaleTicks()) {
    assert(t.raw > last && t.raw <= 255);
    last = t.raw;
  }
  std::printf("ticks:    %zu labelled ticks, ascending, within 0..255\n",
              SMeterScaleTicks().size());

  // ── SWR ───────────────────────────────────────────────────────────────────
  assert(SwrFromRaw(12) == 1.0);
  assert(SwrFromRaw(65) == 1.5);
  assert(SwrFromRaw(89) == 2.0);
  assert(SwrFromRaw(0) == 1.0);       // clamped: a dead reading is not 0:1
  assert(SwrFromRaw(255) == 5.0);     // clamped at the top
  std::printf("swr:      raw 12=1.0, 65=1.5, 89=2.0, clamped 1.0..5.0\n");
  // Monotonic, or a rising SWR could read as falling.
  double prev_swr = SwrFromRaw(0);
  for (int r = 1; r <= 255; ++r) {
    assert(SwrFromRaw(r) >= prev_swr - 1e-9);
    prev_swr = SwrFromRaw(r);
  }
  std::printf("swr:      monotonic across 0..255\n");

  // ── ALC ───────────────────────────────────────────────────────────────────
  // ⚠️ Full scale is raw 64, NOT 255. A naive raw/255 bar would show ALC at a
  // quarter of its real value - under-reading the meter that says the transmit
  // audio is being over-driven.
  assert(AlcPercentFromRaw(64) == 100);
  assert(AlcPercentFromRaw(32) == 50);
  assert(AlcPercentFromRaw(255) == 100);
  const int naive_alc = 64 * 100 / 255;
  std::printf("alc:      raw 64 = 100%% full scale (a raw/255 bar would say %d%%)\n",
              naive_alc);
  assert(naive_alc < 30);

  // ── Power ─────────────────────────────────────────────────────────────────
  // ⚠️ Percent of rated output, never watts. Hamlib's table maps raw 255 to
  // 100 W, and that table is for a 100 W radio; this station's rig is 200 W, so
  // reporting its watts directly would halve every reading.
  assert(PowerPercentFromRaw(0) == 0);
  assert(PowerPercentFromRaw(148) == 50);
  assert(PowerPercentFromRaw(255) == 100);
  std::printf("power:    raw 148 = 50%% of rated, 255 = 100%% - percent, not watts\n");
  std::printf("power:    hamlib's watt table says 100 W at full scale; this rig is "
              "200 W, so watts would read HALF\n");

  std::printf("PASS\n");
  return 0;
}
