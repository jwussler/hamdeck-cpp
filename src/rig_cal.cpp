#include "rig_cal.h"

#include <algorithm>
#include <utility>
#include <format>

const std::vector<CalPoint>& SMeterCalibration() {
  // Hamlib FTDX101D_STR_CAL, verbatim.
  static const std::vector<CalPoint> kCal = {
      {0, -60},   {17, -54},  {25, -48},  {34, -42},
      {51, -36},  {68, -30},  {85, -24},  {102, -18},
      {119, -12}, {136, -6},  {160, 0},   {255, 60},
  };
  return kCal;
}

int RawToDb(int raw) {
  const auto& cal = SMeterCalibration();
  if (raw <= cal.front().raw) return cal.front().db;
  if (raw >= cal.back().raw) return cal.back().db;

  for (size_t i = 1; i < cal.size(); ++i) {
    if (raw <= cal[i].raw) {
      const CalPoint& a = cal[i - 1];
      const CalPoint& b = cal[i];
      const int span = b.raw - a.raw;
      if (span == 0) return a.db;
      return a.db + (raw - a.raw) * (b.db - a.db) / span;
    }
  }
  return cal.back().db;
}

std::string DbToSUnit(int db) {
  if (db >= 0) {
    // Above S9 the scale is reported in dB over S9, which is what operators say.
    // Rounded to the nearest 10 dB because that is the resolution a signal
    // report actually carries - "S9 plus 20", not "S9 plus 17".
    const int over = ((db + 5) / 10) * 10;
    return over == 0 ? "S9" : std::format("S9+{}", over);
  }
  // Each S-unit is 6 dB below S9.
  const int units_below = (-db + 3) / 6;
  const int s = 9 - units_below;
  return std::format("S{}", s < 0 ? 0 : s);
}

const std::vector<ScaleTick>& SMeterScaleTicks() {
  // Odd S-units plus the over-S9 marks: enough to read at a glance without
  // crowding a small meter.
  static const std::vector<ScaleTick> kTicks = {
      {25, "1"},   {51, "3"},   {85, "5"},   {119, "7"},
      {160, "9"},  {192, "+20"}, {223, "+40"}, {255, "+60"},
  };
  return kTicks;
}

namespace {

// Interpolate a raw reading through a table of (raw, value) pairs, clamped at
// both ends - the same shape as Hamlib's rig_raw2val().
double Interp(const std::vector<std::pair<int, double>>& t, int raw) {
  if (raw <= t.front().first) return t.front().second;
  if (raw >= t.back().first) return t.back().second;
  for (size_t i = 1; i < t.size(); ++i) {
    if (raw <= t[i].first) {
      const auto& a = t[i - 1];
      const auto& b = t[i];
      const int span = b.first - a.first;
      if (span == 0) return a.second;
      return a.second + (raw - a.first) * (b.second - a.second) / span;
    }
  }
  return t.back().second;
}

}  // namespace

double SwrFromRaw(int raw) {
  // hamlib yaesu_default_swr_cal, from testing on an FT-991.
  static const std::vector<std::pair<int, double>> kSwr = {
      {12, 1.0}, {39, 1.35}, {65, 1.5}, {89, 2.0}, {242, 5.0},
  };
  return Interp(kSwr, raw);
}

int AlcPercentFromRaw(int raw) {
  // hamlib yaesu_default_alc_cal: raw 64 is FULL SCALE, not raw 255.
  static const std::vector<std::pair<int, double>> kAlc = {{0, 0.0}, {64, 100.0}};
  const double v = Interp(kAlc, raw);
  return static_cast<int>(v + 0.5);
}

int PowerPercentFromRaw(int raw) {
  // hamlib yaesu_default_rfpower_meter_cal, read as PERCENT OF RATED OUTPUT
  // rather than watts - see the warning in the header.
  static const std::vector<std::pair<int, double>> kPwr = {
      {0, 0.0}, {148, 50.0}, {255, 100.0},
  };
  const double v = Interp(kPwr, raw);
  return static_cast<int>(v + 0.5);
}
