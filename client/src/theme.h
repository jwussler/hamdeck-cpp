#pragma once

#include <QString>

// HamDeck palette — the tokens from ~/hamdeck-site/brand/BRAND.md, verbatim.
//
// ⚠️ THESE ARE COPIED, NOT CHOSEN. The same values live in the C# client's
// App.xaml and in the site's CSS, and the brand doc says plainly they must not
// drift. Two implementations each picking their own dark grey is exactly what a
// token table exists to prevent.
//
// ⚠️ THERE IS ONE AMBER: #FFB020. An earlier build of this client used a
// near-miss two digits off, which is exactly the kind of drift the doc calls a
// bug by name. If a second amber turns up anywhere, it is wrong.
// (The literal wrong value is deliberately not written here - it would defeat
//  any grep looking for a stray amber.)
//
// ⚠️ Amber is the BRAND; cyan is the INTERFACE. The mark and marketing lead
// amber; the app keeps cyan for lit controls, because a panel where everything
// glows amber is unreadable.
//
// ⚠️ Red means RF. Never decorative. It marks controls that put the station on
// the air, and that meaning has to stay reliable.
//
// Single theme, deliberately. This is an instrument face; there is no light mode.
namespace theme {

inline constexpr const char* kGround    = "#0E1013";  // panel face, page background
inline constexpr const char* kPanel     = "#171A1F";  // raised tiles and cards
inline constexpr const char* kPanelDeep = "#141619";  // window background
inline constexpr const char* kLine      = "#2A3038";  // hairlines, borders
inline constexpr const char* kText      = "#E8EAED";  // primary text
inline constexpr const char* kDim       = "#8A929C";  // silkscreen labels, captions
inline constexpr const char* kAmber     = "#FFB020";  // the VFO digits. The accent.
inline constexpr const char* kAmberDim  = "#8A6320";  // inactive VFO-B readout
inline constexpr const char* kCyan      = "#3B82F6";  // lit keys, active UI state
inline constexpr const char* kCyanFill  = "#1E3A6B";  // fill behind a lit key
inline constexpr const char* kTxRed     = "#B4232A";  // anything that transmits
inline constexpr const char* kOkGreen   = "#32C765";  // healthy state, S-meter

// Deprecated aliases for the Widgets front end, which is superseded by the QML
// one (CLAUDE.md) and kept only until that is proven. They point at the BRAND
// values, so the old view is brand-correct too rather than carrying the
// invented palette it shipped with.
inline constexpr const char* kBg      = kGround;
inline constexpr const char* kEdge    = kLine;
inline constexpr const char* kTextDim = kDim;
inline constexpr const char* kAccent  = kCyan;
inline constexpr const char* kReadout = kAmber;
inline constexpr const char* kTx      = kTxRed;

QString StyleSheet();   // retained for the Widgets build

}  // namespace theme
