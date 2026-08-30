#pragma once

#include <QString>

// A dark equipment palette. Radios live in dim shacks and get looked at for
// hours; a bright grey settings-dialog is the wrong instrument.
//
// The frequency readout is amber on near-black on purpose: it is the one thing
// read at a glance from across the room, and amber-on-dark is what the gear it
// sits next to uses.
namespace theme {

inline constexpr const char* kBg        = "#14171b";
inline constexpr const char* kPanel     = "#1b1f25";
inline constexpr const char* kEdge      = "#2a3038";
inline constexpr const char* kText      = "#c9d1d9";
inline constexpr const char* kTextDim   = "#7c8794";
inline constexpr const char* kReadout   = "#ffb000";   // amber VFD
inline constexpr const char* kAccent    = "#4a9eff";
inline constexpr const char* kTx        = "#ff3b30";

QString StyleSheet();

}  // namespace theme
