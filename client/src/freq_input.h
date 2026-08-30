#pragma once

// Typing a frequency, the way the reference panel accepts it.
//
// ⚠️ THE FORMS AN OPERATOR ACTUALLY TYPES ARE NOT ONE FORMAT. Ported from
// HamDeck.Remote.Core/FreqInput.cs, whose rules are:
//
//   "14.200"     -> 14 200 000   a decimal below 100 is MHz
//   "7.185"      ->  7 185 000
//   "14"         -> 14 000 000   a bare number below 100 is MHz
//   "14200"      -> 14 200 000   below 100000 is kHz
//   "14200000"   -> 14 200 000   anything larger is Hz
//   "7.185.000"  ->  7 185 000   the GROUPED form the big readout shows
//   "14,200,000" -> 14 200 000   commas and spaces are stripped
//   "abc", ""    -> 0            i.e. refuse
//
// ⚠️ 0 MEANS REFUSE, AND THE CALLER MUST TREAT IT THAT WAY. /api/freq/set moves
// the MODE as well as the frequency, so sending a number nobody asked for is
// not a harmless miss - it can take the rig out of the mode it was working.
//
// ⚠️ THE GROUPED FORM IS WHY THIS IS NOT ONE strtod() CALL. "7.185.000" is what
// the panel displays, so it is what an operator retypes, and it is not a
// decimal number - three dot-separated groups mean digits, not a fraction.

#include <QString>

namespace FreqInput {

// The range /api/freq/set will accept. Outside it, the rig ignores the command.
constexpr long long kMinHz = 30000;
constexpr long long kMaxHz = 75000000;

// Hz, or 0 if the text is not a frequency.
long long Parse(const QString& text);

bool InRange(long long hz);

// 7185000 -> "7.185". Seeds the edit box with something Parse() round-trips.
QString ToEditText(long long hz);

}  // namespace FreqInput
