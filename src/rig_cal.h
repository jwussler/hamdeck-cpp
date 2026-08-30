#pragma once

// S-meter calibration.
//
// ⚠️ THESE NUMBERS ARE NOT GUESSED, AND THEY ARE NOT MEASURED HERE EITHER.
//
// Source: Hamlib's FTDX101D_STR_CAL table (rigs/yaesu/ftdx101.h), which maps the
// rig's raw 0-255 SM0 reading to dB relative to S9. Hamlib's tables come from
// people with the actual radios, which makes them far better than an assumption
// and still short of a lab measurement.
//
// The reason this matters: the obvious guess is that S9 sits at the middle of
// the range, 128. It does not - S9 is at 160. At raw 128 the real reading is
// about S7.5, so the midpoint guess is off by roughly one and a half S-units.
// That is the difference between "you're 5 by 9" and "you're 5 by 7", which is a
// number operators pass on to other people.
//
// ⚠️ Hamlib defines this table for the FTDX-101D; its source file states the
// code is shared with the FTDX-101MP, which has the same receiver and differs in
// PA output. Treated as applying to both. If a reading ever looks wrong on the
// real radio, THIS ASSUMPTION IS THE FIRST THING TO CHECK - and the honest fix
// is to measure the station against a calibrated source, not to nudge the table
// until it looks nicer.

#include <string>
#include <vector>

struct CalPoint {
  int raw;      // 0-255 from SM0
  int db;       // dB relative to S9; S9 = 0, each S-unit = 6 dB
};

// The table, in ascending raw order.
const std::vector<CalPoint>& SMeterCalibration();

// Linear interpolation between points; clamped at both ends, matching how
// Hamlib's rig_raw2val() treats out-of-range values.
int RawToDb(int raw);

// "S0".."S9", then "S9+10" style above S9. Below S0 reports "S0".
std::string DbToSUnit(int db);

// Where the labelled ticks sit, for a client drawing a scale. Returned by the
// host rather than duplicated in every client: the calibration is rig-specific
// knowledge and belongs with the rig, so swapping the radio moves every client's
// scale without shipping a new client.
struct ScaleTick {
  int raw;
  std::string label;
};
const std::vector<ScaleTick>& SMeterScaleTicks();

// ── The other meters ───────────────────────────────────────────────────────
//
// Source: Hamlib's yaesu_default_* calibration tables in rigs/yaesu/newcat.c.
// Each carries a different amount of confidence, and pretending otherwise is
// how a plausible number ends up in front of an operator.

// SWR as a ratio: 1.0, 1.5, 2.0 ...
//
// ⚠️ Hamlib's table is a DEFAULT, from testing on an FT-991 - a different radio.
// The curve shape is right; the exact breakpoints on this rig are unconfirmed.
// Good enough to tell "flat" from "do not transmit"; not good enough to quote to
// three decimals, so it is reported to one.
double SwrFromRaw(int raw);

// ALC as a percentage of full scale. Hamlib maps raw 64 to 1.0, i.e. full scale
// is reached well below the raw maximum - so a naive raw/255 bar would show ALC
// at a quarter of what it really is, which is the wrong direction to be wrong in.
int AlcPercentFromRaw(int raw);

// ⚠️ POWER IS REPORTED AS A PERCENTAGE, NOT WATTS, AND THAT IS DELIBERATE.
//
// Hamlib's table maps raw 255 to 100 W. That table is for a 100 W radio. This
// station's FTDX-101MP is a 200 W radio, so applying it directly would
// UNDER-REPORT transmit power by a factor of two - a wrong number on the one
// meter that tells an operator whether their amplifier is being driven properly.
//
// The honest reading is percentage of rated output, which is what the curve
// actually describes. Watts belong to whoever knows the rig's rating, and this
// host is not told it.
int PowerPercentFromRaw(int raw);
